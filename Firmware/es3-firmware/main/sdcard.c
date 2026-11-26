#include "sdcard.h"

#include <stdio.h>
#include <string.h>
#include "board.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd";
static sdmmc_card_t *s_card;

esp_err_t sd_mount(void)
{
    if (s_card) return ESP_OK;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = LCD_SPI_HOST;             /* the bus is already initialised by main */
    host.max_freq_khz = SD_SPI_KHZ;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = LCD_SPI_HOST;

    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &slot, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "mounted %.7s", s_card->cid.name);
    return ESP_OK;
}

bool sd_is_mounted(void) { return s_card != NULL; }

void sd_describe(char *out, size_t n)
{
    if (!s_card) {
        snprintf(out, n, "SD: no card");
        return;
    }
    double gb = (double)s_card->csd.capacity * s_card->csd.sector_size / (1024.0 * 1024.0 * 1024.0);
    snprintf(out, n, "SD: %.7s %.1f GB", s_card->cid.name, gb);
}

bool sd_selftest(char *msg, size_t n)
{
    if (sd_mount() != ESP_OK) {
        snprintf(msg, n, "SD mount failed");
        return false;
    }
    char out[48], in[48] = {0};
    snprintf(out, sizeof out, "ES3 test %lld", (long long)esp_timer_get_time());

    FILE *f = fopen(SD_MOUNT "/es3_test.txt", "w");
    if (!f) { snprintf(msg, n, "SD: cannot write"); return false; }
    fputs(out, f);
    fclose(f);

    f = fopen(SD_MOUNT "/es3_test.txt", "r");
    if (!f) { snprintf(msg, n, "SD: cannot read back"); return false; }
    if (!fgets(in, sizeof in, f)) in[0] = 0;
    fclose(f);

    if (strcmp(out, in) != 0) { snprintf(msg, n, "SD: data mismatch"); return false; }
    snprintf(msg, n, "SD write/read OK");
    return true;
}
