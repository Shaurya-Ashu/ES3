#pragma once
/* TSC2046 / XPT2046 resistive touch controller on the shared SPI bus. */
#include <stdint.h>
#include <stdbool.h>

#define TOUCH_CAL_MARGIN 20   /* distance of the calibration crosses from the screen edges */

void touch_init(void);                                  /* add the SPI device, load saved calibration */
bool touch_has_calibration(void);
bool touch_read_raw(uint16_t *rx, uint16_t *ry);        /* true while the panel is pressed */
bool touch_get(int16_t *x, int16_t *y);                 /* calibrated screen coordinates */
void touch_calibrate_interactive(void);                 /* 4-point calibration, saved to NVS */
