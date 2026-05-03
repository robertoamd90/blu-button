#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "board_config.h"
#include "battery_telemetry.h"

static const char *TAG = "battery_telemetry";

#define BATTERY_ADC_ATTEN ADC_ATTEN_DB_12
#define BATTERY_ADC_BITWIDTH ADC_BITWIDTH_DEFAULT
#define BATTERY_SAMPLE_COUNT 8
#define BATTERY_PLAUSIBLE_MIN_MV 2500
#define BATTERY_PLAUSIBLE_MAX_MV 5000
#define BATTERY_EMPTY_MV 3200
#define BATTERY_FULL_MV 4200
#define BATTERY_APPROX_FULL_SCALE_MV 2500
#define BATTERY_ADC_RAW_MAX 4095

typedef struct {
    bool initialized;
    bool calibration_enabled;
    adc_cali_scheme_ver_t cali_scheme;
    adc_unit_t unit_id;
    adc_channel_t channel;
    adc_oneshot_unit_handle_t unit_handle;
    adc_cali_handle_t cali_handle;
} battery_adc_state_t;

static battery_adc_state_t s_adc = {0};

static void battery_sample_reset(battery_telemetry_sample_t *sample)
{
    if (!sample) {
        return;
    }

    sample->available = false;
    sample->percent = 0;
    sample->millivolts = 0;
}

static esp_err_t create_cali_handle(adc_unit_t unit_id,
                                    adc_channel_t channel,
                                    adc_cali_handle_t *out_handle,
                                    adc_cali_scheme_ver_t *out_scheme)
{
    adc_cali_scheme_ver_t scheme_mask;
    esp_err_t err;

    if (!out_handle || !out_scheme) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_handle = NULL;
    *out_scheme = 0;

    err = adc_cali_check_scheme(&scheme_mask);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "failed to query ADC calibration scheme");

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if ((scheme_mask & ADC_CALI_SCHEME_VER_CURVE_FITTING) != 0) {
        adc_cali_curve_fitting_config_t cali_cfg = {
            .unit_id = unit_id,
            .chan = channel,
            .atten = BATTERY_ADC_ATTEN,
            .bitwidth = BATTERY_ADC_BITWIDTH,
        };

        err = adc_cali_create_scheme_curve_fitting(&cali_cfg, out_handle);
        if (err == ESP_OK) {
            *out_scheme = ADC_CALI_SCHEME_VER_CURVE_FITTING;
            return ESP_OK;
        }
        if (err != ESP_ERR_NOT_SUPPORTED) {
            return err;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if ((scheme_mask & ADC_CALI_SCHEME_VER_LINE_FITTING) != 0) {
        adc_cali_line_fitting_config_t cali_cfg = {
            .unit_id = unit_id,
            .atten = BATTERY_ADC_ATTEN,
            .bitwidth = BATTERY_ADC_BITWIDTH,
        };

        err = adc_cali_create_scheme_line_fitting(&cali_cfg, out_handle);
        if (err == ESP_OK) {
            *out_scheme = ADC_CALI_SCHEME_VER_LINE_FITTING;
            return ESP_OK;
        }
        if (err != ESP_ERR_NOT_SUPPORTED) {
            return err;
        }
    }
#endif

    return ESP_OK;
}

static esp_err_t ensure_adc_ready(void)
{
    const int battery_gpio = board_config_battery_gpio();
    adc_oneshot_unit_init_cfg_t unit_cfg = {0};
    adc_oneshot_chan_cfg_t channel_cfg = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    esp_err_t err;

    if (battery_gpio < 0) {
        return ESP_OK;
    }

    if (s_adc.initialized) {
        return ESP_OK;
    }

    err = adc_oneshot_io_to_channel(battery_gpio, &s_adc.unit_id, &s_adc.channel);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "battery GPIO %d is not ADC-capable: %s", battery_gpio, esp_err_to_name(err));
        return err;
    }

    unit_cfg.unit_id = s_adc.unit_id;
    err = adc_oneshot_new_unit(&unit_cfg, &s_adc.unit_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = adc_oneshot_config_channel(s_adc.unit_handle, s_adc.channel, &channel_cfg);
    if (err != ESP_OK) {
        (void)adc_oneshot_del_unit(s_adc.unit_handle);
        s_adc.unit_handle = NULL;
        return err;
    }

    err = create_cali_handle(s_adc.unit_id, s_adc.channel, &s_adc.cali_handle, &s_adc.cali_scheme);
    if (err != ESP_OK) {
        (void)adc_oneshot_del_unit(s_adc.unit_handle);
        s_adc.unit_handle = NULL;
        return err;
    }

    s_adc.calibration_enabled = s_adc.cali_handle != NULL;
    s_adc.initialized = true;
    return ESP_OK;
}

static esp_err_t read_pin_voltage_mv(uint32_t *out_mv)
{
    int raw_sum = 0;
    int voltage_sum = 0;
    int raw = 0;
    esp_err_t err;

    if (!out_mv) {
        return ESP_ERR_INVALID_ARG;
    }

    err = ensure_adc_ready();
    if (err != ESP_OK) {
        return err;
    }

    if (!s_adc.unit_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < BATTERY_SAMPLE_COUNT; i++) {
        err = adc_oneshot_read(s_adc.unit_handle, s_adc.channel, &raw);
        if (err != ESP_OK) {
            return err;
        }

        raw_sum += raw;
        if (s_adc.calibration_enabled) {
            int voltage_mv = 0;

            err = adc_cali_raw_to_voltage(s_adc.cali_handle, raw, &voltage_mv);
            if (err != ESP_OK) {
                return err;
            }
            voltage_sum += voltage_mv;
        }
    }

    if (s_adc.calibration_enabled) {
        *out_mv = (uint32_t)(voltage_sum / BATTERY_SAMPLE_COUNT);
    } else {
        *out_mv = (uint32_t)(((int64_t)raw_sum * BATTERY_APPROX_FULL_SCALE_MV) /
                             (BATTERY_ADC_RAW_MAX * BATTERY_SAMPLE_COUNT));
    }

    return ESP_OK;
}

static esp_err_t scale_to_battery_mv(uint32_t pin_mv, uint32_t *out_battery_mv)
{
    const uint32_t divider_num = board_config_battery_divider_num();
    const uint32_t divider_den = board_config_battery_divider_den();

    if (!out_battery_mv || divider_num == 0 || divider_den == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_battery_mv = (uint32_t)(((uint64_t)pin_mv * divider_num) / divider_den);
    return ESP_OK;
}

static uint8_t battery_percent_from_mv(uint32_t battery_mv)
{
    if (battery_mv <= BATTERY_EMPTY_MV) {
        return 0;
    }

    if (battery_mv >= BATTERY_FULL_MV) {
        return 100;
    }

    return (uint8_t)(((battery_mv - BATTERY_EMPTY_MV) * 100U) / (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
}

esp_err_t battery_telemetry_sample(battery_telemetry_sample_t *out_sample)
{
    uint32_t pin_mv = 0;
    esp_err_t err;

    if (!out_sample) {
        return ESP_ERR_INVALID_ARG;
    }

    battery_sample_reset(out_sample);

    if (board_config_battery_gpio() < 0) {
        return ESP_OK;
    }

    err = read_pin_voltage_mv(&pin_mv);
    if (err != ESP_OK) {
        return err;
    }

    err = scale_to_battery_mv(pin_mv, &out_sample->millivolts);
    if (err != ESP_OK) {
        return err;
    }

    if (out_sample->millivolts < BATTERY_PLAUSIBLE_MIN_MV ||
        out_sample->millivolts > BATTERY_PLAUSIBLE_MAX_MV) {
        ESP_LOGW(TAG, "battery reading %" PRIu32 " mV outside plausible range; omitting telemetry",
                 out_sample->millivolts);
        out_sample->millivolts = 0;
        return ESP_OK;
    }

    out_sample->percent = battery_percent_from_mv(out_sample->millivolts);
    out_sample->available = true;
    ESP_LOGI(TAG, "battery sample %" PRIu32 " mV -> %u%%",
             out_sample->millivolts,
             (unsigned)out_sample->percent);
    return ESP_OK;
}
