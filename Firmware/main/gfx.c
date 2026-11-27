#include "gfx.h"

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "font8x16.h"

static const char *TAG = "gfx";

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_fb;      
static uint16_t *s_dma;      
static SemaphoreHandle_t s_done;

static struct {
    int x0, y0, x1, y1;
    bool dirty;
} s_dirty;

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &hp);
    return hp == pdTRUE;
}

static void mark(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    int x1 = x + w - 1, y1 = y + h - 1;
    if (!s_dirty.dirty) {
        s_dirty.x0 = x; s_dirty.y0 = y; s_dirty.x1 = x1; s_dirty.y1 = y1;
        s_dirty.dirty = true;
        return;
    }
    if (x < s_dirty.x0) s_dirty.x0 = x;
    if (y < s_dirty.y0) s_dirty.y0 = y;
    if (x1 > s_dirty.x1) s_dirty.x1 = x1;
    if (y1 > s_dirty.y1) s_dirty.y1 = y1;
}

static bool clip(int *x, int *y, int *w, int *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > LCD_W) *w = LCD_W - *x;
    if (*y + *h > LCD_H) *h = LCD_H - *y;
    return *w > 0 && *h > 0;
}

static void backlight_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .gpio_num = PIN_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

void gfx_set_backlight(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)(percent * 255 / 100));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void gfx_init(void)
{
    s_done = xSemaphoreCreateBinary();

    s_fb = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_fb) {
        ESP_LOGW(TAG, "no PSRAM canvas, using internal RAM");
        s_fb = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_8BIT);
    }
    s_dma = heap_caps_malloc(GFX_MAX_TRANSFER, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(s_fb && s_dma);

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = LCD_SPI_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_trans_done,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &io));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, LCD_INVERT_COLOR));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X, LCD_MIRROR_Y));

    /* Clear to black before turning the panel and backlight on so there is no flash of garbage. */
    memset(s_fb, 0, LCD_W * LCD_H * 2);
    mark(0, 0, LCD_W, LCD_H);
    gfx_flush();
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    backlight_init();
    gfx_set_backlight(100);
    ESP_LOGI(TAG, "display ready, %d x %d @ %d MHz", LCD_W, LCD_H, LCD_SPI_HZ / 1000000);
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    if (!clip(&x, &y, &w, &h)) return;
    for (int j = 0; j < h; j++) {
        uint16_t *p = &s_fb[(y + j) * LCD_W + x];
        for (int i = 0; i < w; i++) p[i] = c;
    }
    mark(x, y, w, h);
}

void gfx_rect(int x, int y, int w, int h, uint16_t c)
{
    gfx_fill_rect(x, y, w, 1, c);
    gfx_fill_rect(x, y + h - 1, w, 1, c);
    gfx_fill_rect(x, y, 1, h, c);
    gfx_fill_rect(x + w - 1, y, 1, h, c);
}

void gfx_text(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    if (y < 0 || y + FONT_H > LCD_H) return;
    int x_start = x;
    for (; *s; s++, x += FONT_W) {
        if (x < 0 || x + FONT_W > LCD_W) continue;
        unsigned char ch = (unsigned char)*s;
        if (ch < FONT_FIRST || ch > FONT_LAST) ch = '?';
        const uint8_t *g = font8x16[ch - FONT_FIRST];
        for (int r = 0; r < FONT_H; r++) {
            uint16_t *p = &s_fb[(y + r) * LCD_W + x];
            uint8_t bits = g[r];
            for (int c = 0; c < FONT_W; c++) p[c] = (bits & (0x80 >> c)) ? fg : bg;
        }
    }
    int x0 = x_start < 0 ? 0 : x_start;
    int x1 = x > LCD_W ? LCD_W : x;
    mark(x0, y, x1 - x0, FONT_H);
}

void gfx_flush(void)
{
    if (!s_dirty.dirty) return;
    int x0 = s_dirty.x0, x1 = s_dirty.x1, y0 = s_dirty.y0, y1 = s_dirty.y1;
    s_dirty.dirty = false;

    int w = x1 - x0 + 1;
    for (int y = y0; y <= y1; y += GFX_STRIP_ROWS) {
        int rows = (y1 - y + 1 < GFX_STRIP_ROWS) ? (y1 - y + 1) : GFX_STRIP_ROWS;
        for (int r = 0; r < rows; r++) {
            memcpy(s_dma + r * w, &s_fb[(y + r) * LCD_W + x0], (size_t)w * 2);
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, x0, y, x0 + w, y + rows, s_dma));
        if (xSemaphoreTake(s_done, pdMS_TO_TICKS(200)) != pdTRUE) {
            ESP_LOGW(TAG, "flush timeout");
        }
    }
}
