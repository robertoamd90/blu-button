#pragma once

#include "esp_err.h"
#include "button_event.h"

esp_err_t led_feedback_init(void);
/* Runs LED feedback synchronously in FreeRTOS task context and returns only
 * after the pattern completes with the LED off again.
 * `led_feedback_init()` must have succeeded first.
 * Returns ESP_ERR_INVALID_STATE if init did not succeed and
 * ESP_ERR_NOT_SUPPORTED for events without LED feedback. */
esp_err_t led_feedback_run_button_event_pattern_blocking(button_event_t event);
