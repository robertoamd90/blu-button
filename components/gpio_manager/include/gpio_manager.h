#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "button_event.h"

typedef esp_err_t (*gpio_manager_wake_capture_hook_t)(void *ctx);

bool gpio_manager_boot_button_pressed(void);
esp_err_t gpio_manager_capture_wake_event(uint32_t wakeup_causes,
                                          gpio_manager_wake_capture_hook_t during_capture,
                                          void *ctx,
                                          button_event_t *out_event,
                                          bool *out_have_event);
esp_err_t gpio_manager_enable_boot_button_wakeup(void);
