/* ES3 board bring-up firmware.
 *
 * Exercises everything on the board and shows it on the display:
 *   - ST7789V display (SPI, backlight PWM)
 *   - TSC2046 touch (SPI, shared bus) with 4-point calibration saved in NVS
 *   - microSD card (SPI, shared bus)
 *   - three buttons on IO21 / IO47 / IO48 (active low)
 *   - battery voltage on IO1
 *
 * Buttons:  A = backlight step   B = recalibrate touch   C = SD write/read test
 * Touch:    draw in the lower area, CLEAR button wipes it.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "board.h"
#include "gfx.h"
#include "touch.h"
#include "battery.h"
#include "sdcard.h"

static const char *TAG = "es3";

/* Screen layout (240 x 320, portrait) */
#define Y_BATT_TXT   24
#define Y_BATT_BAR   44
#define Y_SD         62
#define Y_BTN        82
#define Y_TOUCH     104
#define Y_MSG       126
#define Y_HINT      146
#define PAINT_X       4
#define PAINT_Y     168
#define PAINT_W     232
#define PAINT_H     148
#define CLEAR_X     176
#define CLEAR_Y     102
#define CLEAR_W      60
#define CLEAR_H      20

typedef struct {
    int pin;
    bool state;     /* debounced, true = pressed */
    int count;
    bool pressed_edge;
} button_t;

static button_t s_btn[3] = {{.pin = PIN_SW_A}, {.pin = PIN_SW_B}, {.pin = PIN_SW_C}};
static const int k_bright[] = {25, 50, 75, 100};
static int s_bright_idx = 3;
static char s_sd_line[40];

static void spi_bus_setup(void)
{
    spi_bus_config_t cfg = {
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .sclk_io_num = PIN_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = GFX_MAX_TRANSFER,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &cfg, SPI_DMA_CH_AUTO));
}

static void buttons_init(void)
{
    for (int i = 0; i < 3; i++) {
        gpio_config_t c = {
            .pin_bit_mask = 1ULL << s_btn[i].pin,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&c));
    }
}

static void buttons_poll(void)   /* call every 10 ms */
{
    for (int i = 0; i < 3; i++) {
        bool raw = (gpio_get_level(s_btn[i].pin) == 0);
        if (raw != s_btn[i].state) {
            if (++s_btn[i].count >= 3) {
                s_btn[i].state = raw;
                s_btn[i].count = 0;
                if (raw) s_btn[i].pressed_edge = true;
            }
        } else {
            s_btn[i].count = 0;
        }
    }
}

static bool button_pressed_edge(int i)
{
    bool e = s_btn[i].pressed_edge;
    s_btn[i].pressed_edge = false;
    return e;
}

/* ---------- drawing ---------- */

static void line(int y, uint16_t fg, const char *fmt, ...)
{
    char tmp[48], out[32];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    snprintf(out, sizeof out, "%-29.29s", tmp);
    gfx_text(4, y, out, fg, COL_BG);
}

static void paint_clear(void)
{
    gfx_fill_rect(PAINT_X, PAINT_Y, PAINT_W, PAINT_H, COL_CANVAS);
}

static void ui_static(void)
{
    gfx_fill_rect(0, 0, LCD_W, LCD_H, COL_BG);
    gfx_fill_rect(0, 0, LCD_W, 20, COL_TITLE);
    gfx_text(8, 2, "ES3 board bring-up", COL_TEXT, COL_TITLE);
    gfx_text(4, Y_BTN, "Buttons", COL_DIM, COL_BG);
    gfx_text(4, Y_HINT, "A:light  B:calib  C:SD test", COL_DIM, COL_BG);
    line(Y_TOUCH, COL_DIM, "Touch --");

    gfx_fill_rect(CLEAR_X, CLEAR_Y, CLEAR_W, CLEAR_H, COL_BOX);
    gfx_text(CLEAR_X + 10, CLEAR_Y + 2, "CLEAR", COL_TEXT, COL_BOX);

    gfx_rect(PAINT_X - 1, PAINT_Y - 1, PAINT_W + 2, PAINT_H + 2, COL_DIM);
    paint_clear();
}

static void ui_buttons(void)
{
    static const char names[3] = {'A', 'B', 'C'};
    for (int i = 0; i < 3; i++) {
        uint16_t bg = s_btn[i].state ? COL_TITLE : COL_BOX;
        int x = 72 + i * 40;
        gfx_fill_rect(x, Y_BTN, 32, 16, bg);
        char s[2] = {names[i], 0};
        gfx_text(x + 12, Y_BTN, s, COL_TEXT, bg);
    }
}

static void ui_battery(void)
{
    float v = battery_voltage();
    int pct = battery_percent(v);
    line(Y_BATT_TXT, COL_TEXT, "Battery %.2f V  %d%%", v, pct);

    uint16_t col = pct > 30 ? COL_GOOD : (pct > 15 ? COL_WARN : COL_BAD);
    const int w = 232;
    gfx_rect(4, Y_BATT_BAR, w, 12, COL_DIM);
    gfx_fill_rect(5, Y_BATT_BAR + 1, w - 2, 10, COL_BG);
    gfx_fill_rect(5, Y_BATT_BAR + 1, (w - 2) * pct / 100, 10, col);
}

static void ui_sd(void)
{
    sd_describe(s_sd_line, sizeof s_sd_line);
    line(Y_SD, sd_is_mounted() ? COL_GOOD : COL_WARN, "%s", s_sd_line);
}

static void ui_message(uint16_t col, const char *msg)
{
    line(Y_MSG, col, "%s", msg);
}

static void splash(void)
{
    /* Colour bars: red, green, blue, white, black. If the colours look wrong,
       toggle LCD_INVERT_COLOR in board.h. */
    const uint16_t bars[5] = {rgb565(255, 0, 0), rgb565(0, 255, 0), rgb565(0, 0, 255),
                              rgb565(255, 255, 255), rgb565(0, 0, 0)};
    for (int i = 0; i < 5; i++) gfx_fill_rect(i * (LCD_W / 5), 0, LCD_W / 5, LCD_H, bars[i]);
    gfx_text(8, 150, "ES3 starting...", rgb565(255, 255, 255), rgb565(0, 0, 0));
    gfx_flush();
    vTaskDelay(pdMS_TO_TICKS(900));
}

/* ---------- main ---------- */

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    spi_bus_setup();
    gfx_init();            /* display first, so the screen shows what happens next */
    splash();
    touch_init();          /* add every SPI device before the SD card is mounted, */
    battery_init();        /* so all chip selects idle high during SD initialisation */
    buttons_init();
    sd_mount();

    if (!touch_has_calibration()) touch_calibrate_interactive();

    ui_static();
    ui_buttons();
    ui_battery();
    ui_sd();
    ui_message(COL_DIM, "Ready");
    gfx_set_backlight(k_bright[s_bright_idx]);
    gfx_flush();
    ESP_LOGI(TAG, "ready: %s, battery %.2f V", s_sd_line, battery_voltage());

    int64_t next_stats = 0;
    bool was_down = false;
    int16_t last_x = -1, last_y = -1;

    for (;;) {
        int64_t now = esp_timer_get_time();

        /* buttons */
        bool prev[3] = {s_btn[0].state, s_btn[1].state, s_btn[2].state};
        buttons_poll();
        if (prev[0] != s_btn[0].state || prev[1] != s_btn[1].state || prev[2] != s_btn[2].state) ui_buttons();

        if (button_pressed_edge(0)) {
            s_bright_idx = (s_bright_idx + 1) % (int)(sizeof(k_bright) / sizeof(k_bright[0]));
            gfx_set_backlight(k_bright[s_bright_idx]);
            char m[32];
            snprintf(m, sizeof m, "Backlight %d%%", k_bright[s_bright_idx]);
            ui_message(COL_TEXT, m);
        }
        if (button_pressed_edge(1)) {
            touch_calibrate_interactive();
            ui_static();
            ui_buttons();
            ui_battery();
            ui_sd();
            ui_message(COL_GOOD, "Calibrated");
        }
        if (button_pressed_edge(2)) {
            char m[32];
            ui_message(COL_DIM, "Testing SD card...");
            gfx_flush();
            bool ok = sd_selftest(m, sizeof m);
            ui_message(ok ? COL_GOOD : COL_BAD, m);
            ui_sd();
        }

        /* touch */
        int16_t x, y;
        bool down = touch_get(&x, &y);
        if (down) {
            if (x >= CLEAR_X && x < CLEAR_X + CLEAR_W && y >= CLEAR_Y && y < CLEAR_Y + CLEAR_H) {
                if (!was_down) {
                    paint_clear();
                    ui_message(COL_DIM, "Canvas cleared");
                }
            } else if (x >= PAINT_X + 1 && x < PAINT_X + PAINT_W - 1 &&
                       y >= PAINT_Y + 1 && y < PAINT_Y + PAINT_H - 1) {
                gfx_fill_rect(x - 1, y - 1, 3, 3, COL_PEN);
            }
            if (x != last_x || y != last_y) {
                line(Y_TOUCH, COL_TEXT, "Touch x=%3d y=%3d", x, y);
                last_x = x;
                last_y = y;
            }
        } else if (was_down) {
            line(Y_TOUCH, COL_DIM, "Touch --");
            last_x = last_y = -1;
        }
        was_down = down;

        /* slow status refresh */
        if (now >= next_stats) {
            ui_battery();
            next_stats = now + 500000;
        }

        gfx_flush();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
