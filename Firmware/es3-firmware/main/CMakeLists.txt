idf_component_register(
    SRCS "main.c" "gfx.c" "touch.c" "battery.c" "sdcard.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_adc esp_timer fatfs sdmmc nvs_flash
)
