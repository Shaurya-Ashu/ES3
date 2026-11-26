#include "battery.h"

#include "board.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "battery";
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;

void battery_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = BAT_ADC_UNIT};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &s_adc));

    adc_oneshot_chan_cfg_t ch = {
        .atten = ADC_ATTEN_DB_12,           /* about 0..3.1 V, the divider gives at most 2.1 V */
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, BAT_ADC_CH, &ch));

    adc_cali_curve_fitting_config_t cal = {
        .unit_id = BAT_ADC_UNIT,
        .chan = BAT_ADC_CH,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) {
        s_cali = NULL;
        ESP_LOGW(TAG, "no ADC calibration data, readings are approximate");
    }
}

float battery_voltage(void)
{
    int sum = 0, n = 0;
    for (int i = 0; i < 16; i++) {
        int raw;
        if (adc_oneshot_read(s_adc, BAT_ADC_CH, &raw) == ESP_OK) { sum += raw; n++; }
    }
    if (n == 0) return 0.0f;
    int raw = sum / n, mv;
    if (s_cali) {
        if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) mv = raw * 3100 / 4095;
    } else {
        mv = raw * 3100 / 4095;
    }
    return mv * BAT_DIV_RATIO / 1000.0f;
}

int battery_percent(float v)
{
    static const struct { float v; int pct; } t[] = {
        {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.90f, 65}, {3.80f, 50},
        {3.70f, 35},  {3.60f, 20}, {3.50f, 10}, {3.30f, 0},
    };
    const int n = sizeof(t) / sizeof(t[0]);
    if (v >= t[0].v) return 100;
    if (v <= t[n - 1].v) return 0;
    for (int i = 0; i < n - 1; i++) {
        if (v <= t[i].v && v > t[i + 1].v) {
            float f = (v - t[i + 1].v) / (t[i].v - t[i + 1].v);
            return (int)(t[i + 1].pct + f * (t[i].pct - t[i + 1].pct) + 0.5f);
        }
    }
    return 0;
}
