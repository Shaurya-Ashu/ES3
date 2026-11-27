#pragma once
/* Pin map and board options for the ES3 board (schematic V1.0, ESP32-S3-WROOM-1 N16R8). */
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "esp_adc/adc_oneshot.h"

/* Shared SPI2 bus: display (ST7789V), touch (TSC2046) and SD card */
#define LCD_SPI_HOST   SPI2_HOST
#define PIN_SPI_MOSI   11
#define PIN_SPI_SCLK   12
#define PIN_SPI_MISO   13

/* Display */
#define PIN_LCD_CS     10
#define PIN_LCD_DC      9
#define PIN_LCD_RST     8
#define PIN_LCD_BL     14   
#define LCD_W          240
#define LCD_H          320
#define LCD_SPI_HZ     (40 * 1000 * 1000)  
#define LCD_INVERT_COLOR 1  
#define LCD_MIRROR_X     0
#define LCD_MIRROR_Y     0


#define PIN_TOUCH_CS   15
#define PIN_TOUCH_IRQ  16   
#define TOUCH_SPI_HZ   (2 * 1000 * 1000)

/* SD card (SPI mode) */
#define PIN_SD_CS      17
#define SD_MOUNT       "/sdcard"
#define SD_SPI_KHZ     20000

/* Buttons, active low  */
#define PIN_SW_A       21
#define PIN_SW_B       47
#define PIN_SW_C       48

/* Battery sense */
#define BAT_ADC_UNIT   ADC_UNIT_1
#define BAT_ADC_CH     ADC_CHANNEL_0   
#define BAT_DIV_RATIO  2.0f
