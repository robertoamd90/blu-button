#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool available;
    uint8_t percent;
    uint32_t millivolts;
} battery_telemetry_sample_t;

esp_err_t battery_telemetry_sample(battery_telemetry_sample_t *out_sample);
