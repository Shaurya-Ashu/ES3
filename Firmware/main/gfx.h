#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "board.h"

#define GFX_STRIP_ROWS    20
#define GFX_MAX_TRANSFER  (LCD_W * GFX_STRIP_ROWS * 2)


static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((v >> 8) | (v << 8));
}

#define COL_BG      rgb565(14, 17, 22)
#define COL_CANVAS  rgb565(24, 28, 36)
#define COL_TITLE   rgb565(31, 95, 214)
#define COL_TEXT    rgb565(225, 232, 240)
#define COL_DIM     rgb565(130, 145, 160)
#define COL_GOOD    rgb565(60, 200, 120)
#define COL_WARN    rgb565(240, 170, 40)
#define COL_BAD     rgb565(230, 80, 70)
#define COL_BOX     rgb565(44, 54, 68)
#define COL_PEN     rgb565(255, 220, 90)

void gfx_init(void);                       /* needs the SPI bus initialised first */
void gfx_set_backlight(int percent);       /* 0..100 */
void gfx_fill_rect(int x, int y, int w, int h, uint16_t c);
void gfx_rect(int x, int y, int w, int h, uint16_t c);   /* 1 px outline */
void gfx_text(int x, int y, const char *s, uint16_t fg, uint16_t bg);  /* 8x16 font */
void gfx_flush(void);                      /* push everything drawn since the last flush */
