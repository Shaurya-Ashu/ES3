#pragma once
/* microSD over the shared SPI bus, mounted as FAT at /sdcard. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t sd_mount(void);             
bool sd_is_mounted(void);
void sd_describe(char *out, size_t n);        
bool sd_selftest(char *msg, size_t n);        
