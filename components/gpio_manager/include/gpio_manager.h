#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "button_event.h"

/*
 * Single-use wake-capture session owned by the runtime for one wake cycle.
 * Call begin() soon after wake, perform any overlap-safe wake work, then call
 * finish() exactly once in the same wake cycle to classify the gesture.
 */
typedef struct {
    uint32_t wakeup_causes;
    bool armed;
    size_t active_button;
    size_t button_count;
} gpio_wake_capture_t;

bool gpio_manager_button_pressed(void);
esp_err_t gpio_manager_begin_wake_capture(gpio_wake_capture_t *capture,
                                          uint32_t wakeup_causes);
esp_err_t gpio_manager_finish_wake_capture(const gpio_wake_capture_t *capture,
                                           button_event_t *out_event,
                                           bool *out_have_event);
esp_err_t gpio_manager_enable_button_wakeup(void);
