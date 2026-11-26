# ES3 board bring-up firmware

ESP-IDF (5.2 or newer) project for the ES3 board: ESP32-S3-WROOM-1 N16R8, 2.4" ST7789V display with
TSC2046 touch, microSD, three buttons and a battery sense input.

Compiles cleanly with ESP-IDF v5.3.2 (binary about 380 KB). It has not been run on hardware yet.

## Build and flash

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build flash monitor        # add -p <port>; use the CH340X port or the native USB port
```

## What it does

- Shows colour bars for a second so you can check colours (if black and white look swapped, set
  `LCD_INVERT_COLOR` to 0 in `main/board.h`).
- First boot: runs a 4-point touch calibration and saves it to flash (NVS).
- Dashboard: battery voltage and percentage, SD card name and size, live state of the three buttons,
  touch coordinates, and a drawing area (CLEAR wipes it).
- Button A steps the backlight (25, 50, 75, 100 %), B recalibrates touch, C writes and reads back
  `/sdcard/es3_test.txt`.
- Logs over the CH340X UART (IO43/IO44) and the native USB port.

## Pins (from the schematic)

| Function | GPIO |
|---|---|
| SPI MOSI / SCLK / MISO (shared by display, touch, SD) | 11 / 12 / 13 |
| LCD CS / DC / RST / backlight | 10 / 9 / 8 / 14 |
| Touch CS / PENIRQ | 15 / 16 |
| SD CS | 17 |
| Buttons A / B / C (active low) | 21 / 47 / 48 |
| Battery sense (divider 1:2) | 1 |

All pins and options live in `main/board.h`.

## Files

- `main/main.c`: start-up order, dashboard, button and touch handling
- `main/gfx.c`: RGB565 canvas in PSRAM, dirty-rectangle flush over SPI DMA, 8x16 text, backlight PWM
- `main/touch.c`: TSC2046 driver, median filtering, calibration with axis swap and inversion detection
- `main/sdcard.c`: SD over SPI on the shared bus, write and read-back self test
- `main/battery.c`: ADC with calibration, LiPo percentage curve
- `tools/gen_font.py`: regenerates `main/font8x16.h` from DejaVu Sans Mono

## Notes

- Start-up order matters: every SPI device is added before the SD card is mounted so all chip selects
  idle high during SD initialisation.
- The display runs at 40 MHz. Raise `LCD_SPI_HZ` once the wiring is proven. The SD card runs at 20 MHz and
  the touch controller at 2 MHz.
- Battery percentage is a rough LiPo curve. Calibrate against a meter before trusting it.
