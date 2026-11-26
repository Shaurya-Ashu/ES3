#include "touch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "gfx.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "touch";

/* Control bytes: start bit, channel, 12-bit, differential, power down between conversions. */
#define CMD_X 0xD0
#define CMD_Y 0x90
#define SAMPLES 7
#define CAL_MAGIC 0x45533301u

typedef struct {
    uint32_t magic;
    int32_t swap;      /* 1 if screen X follows the raw Y channel */
    int32_t L, R, T, B;
} cal_t;

static spi_device_handle_t s_dev;
static cal_t s_cal;
static bool s_cal_ok;

static uint16_t xfer(uint8_t cmd)
{
    uint8_t tx[3] = {cmd, 0, 0};
    uint8_t rx[3] = {0};
    spi_transaction_t t = {
        .length = 24,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_dev, &t));
    return (uint16_t)((((rx[1] << 8) | rx[2]) >> 3) & 0x0FFF);
}

static void sort_u16(uint16_t *a, int n)
{
    for (int i = 1; i < n; i++) {
        uint16_t v = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
        a[j + 1] = v;
    }
}

static esp_err_t cal_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("es3", NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t sz = sizeof(cal_t);
    cal_t c;
    err = nvs_get_blob(h, "touch", &c, &sz);
    nvs_close(h);
    if (err != ESP_OK || sz != sizeof(cal_t) || c.magic != CAL_MAGIC) return ESP_FAIL;
    s_cal = c;
    return ESP_OK;
}

static void cal_save(void)
{
    nvs_handle_t h;
    if (nvs_open("es3", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "touch", &s_cal, sizeof(s_cal));
    nvs_commit(h);
    nvs_close(h);
}

void touch_init(void)
{
    gpio_config_t irq = {
        .pin_bit_mask = 1ULL << PIN_TOUCH_IRQ,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,   /* the board has a 100k pull-up */
    };
    ESP_ERROR_CHECK(gpio_config(&irq));

    spi_device_interface_config_t dc = {
        .clock_speed_hz = TOUCH_SPI_HZ,
        .mode = 0,
        .spics_io_num = PIN_TOUCH_CS,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &dc, &s_dev));

    s_cal_ok = (cal_load() == ESP_OK);
    ESP_LOGI(TAG, "touch ready, calibration %s", s_cal_ok ? "loaded" : "missing");
}

bool touch_has_calibration(void) { return s_cal_ok; }

bool touch_read_raw(uint16_t *rx, uint16_t *ry)
{
    if (gpio_get_level(PIN_TOUCH_IRQ)) return false;   /* PENIRQ# is high when untouched */

    uint16_t xs[SAMPLES], ys[SAMPLES];
    (void)xfer(CMD_X);                                 /* throw away the first conversion */
    for (int i = 0; i < SAMPLES; i++) {
        xs[i] = xfer(CMD_X);
        ys[i] = xfer(CMD_Y);
    }
    if (gpio_get_level(PIN_TOUCH_IRQ)) return false;   /* released while sampling */

    sort_u16(xs, SAMPLES);
    sort_u16(ys, SAMPLES);
    uint16_t x = xs[SAMPLES / 2], y = ys[SAMPLES / 2];
    if (x < 50 || x > 4050 || y < 50 || y > 4050) return false;   /* open circuit / noise */
    *rx = x;
    *ry = y;
    return true;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool touch_get(int16_t *x, int16_t *y)
{
    uint16_t rx, ry;
    if (!touch_read_raw(&rx, &ry)) return false;
    if (!s_cal_ok) {   /* rough guess until calibrated */
        *x = (int16_t)(rx * LCD_W / 4096);
        *y = (int16_t)(ry * LCD_H / 4096);
        return true;
    }
    int u = s_cal.swap ? ry : rx;
    int v = s_cal.swap ? rx : ry;
    int m = TOUCH_CAL_MARGIN;
    int sx = m + (u - s_cal.L) * (LCD_W - 1 - 2 * m) / (s_cal.R - s_cal.L);
    int sy = m + (v - s_cal.T) * (LCD_H - 1 - 2 * m) / (s_cal.B - s_cal.T);
    *x = (int16_t)clampi(sx, 0, LCD_W - 1);
    *y = (int16_t)clampi(sy, 0, LCD_H - 1);
    return true;
}

/* Wait for a clean press and return the averaged raw position. */
static void wait_press(uint16_t *rx, uint16_t *ry)
{
    uint16_t x, y;
    while (touch_read_raw(&x, &y)) vTaskDelay(pdMS_TO_TICKS(10));   /* wait for release */
    vTaskDelay(pdMS_TO_TICKS(150));
    while (!touch_read_raw(&x, &y)) vTaskDelay(pdMS_TO_TICKS(10));  /* wait for press */

    uint32_t sx = x, sy = y;
    int n = 1;
    for (int i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (touch_read_raw(&x, &y)) { sx += x; sy += y; n++; }
    }
    *rx = (uint16_t)(sx / n);
    *ry = (uint16_t)(sy / n);
}

static void draw_cross(int x, int y)
{
    gfx_fill_rect(x - 10, y - 1, 21, 3, COL_WARN);
    gfx_fill_rect(x - 1, y - 10, 3, 21, COL_WARN);
}

void touch_calibrate_interactive(void)
{
    const int m = TOUCH_CAL_MARGIN;
    const int px[4] = {m, LCD_W - 1 - m, LCD_W - 1 - m, m};
    const int py[4] = {m, m, LCD_H - 1 - m, LCD_H - 1 - m};
    const char *where[4] = {"top left", "top right", "bottom right", "bottom left"};

    for (;;) {
        uint16_t rx[4], ry[4];
        for (int i = 0; i < 4; i++) {
            char msg[32];
            gfx_fill_rect(0, 0, LCD_W, LCD_H, COL_BG);
            gfx_text(8, 100, "Touch calibration", COL_TEXT, COL_BG);
            snprintf(msg, sizeof msg, "Tap the cross (%d/4)", i + 1);
            gfx_text(8, 124, msg, COL_DIM, COL_BG);
            snprintf(msg, sizeof msg, "%s", where[i]);
            gfx_text(8, 144, msg, COL_DIM, COL_BG);
            draw_cross(px[i], py[i]);
            gfx_flush();
            wait_press(&rx[i], &ry[i]);
            ESP_LOGI(TAG, "cal point %d raw x=%u y=%u", i, rx[i], ry[i]);
        }

        int dxh = abs((int)rx[1] - (int)rx[0]);
        int dyh = abs((int)ry[1] - (int)ry[0]);
        bool swap = dyh > dxh;
        int u[4], v[4];
        for (int i = 0; i < 4; i++) {
            u[i] = swap ? ry[i] : rx[i];
            v[i] = swap ? rx[i] : ry[i];
        }
        cal_t c = {
            .magic = CAL_MAGIC,
            .swap = swap,
            .L = (u[0] + u[3]) / 2,
            .R = (u[1] + u[2]) / 2,
            .T = (v[0] + v[1]) / 2,
            .B = (v[2] + v[3]) / 2,
        };
        if (abs(c.R - c.L) > 600 && abs(c.B - c.T) > 600) {
            s_cal = c;
            s_cal_ok = true;
            cal_save();
            ESP_LOGI(TAG, "calibrated: swap=%d L=%d R=%d T=%d B=%d", (int)c.swap, (int)c.L, (int)c.R, (int)c.T, (int)c.B);
            break;
        }
        gfx_fill_rect(0, 0, LCD_W, LCD_H, COL_BG);
        gfx_text(8, 140, "Bad readings, try again", COL_BAD, COL_BG);
        gfx_flush();
        vTaskDelay(pdMS_TO_TICKS(1200));
    }

    gfx_fill_rect(0, 0, LCD_W, LCD_H, COL_BG);
    gfx_text(8, 150, "Calibration saved", COL_GOOD, COL_BG);
    gfx_flush();
    vTaskDelay(pdMS_TO_TICKS(700));
}
