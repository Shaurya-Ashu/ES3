#pragma once
/* microSD over the shared SPI bus, mounted as FAT at /sdcard. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t sd_mount(void);             /* safe to call again, e.g. after inserting a card */
bool sd_is_mounted(void);
void sd_describe(char *out, size_t n);        /* "SD: ABCDE 14.8 GB" or "SD: no card" */
bool sd_selftest(char *msg, size_t n);        /* write a file, read it back, compare */
