#pragma once

#include "esp_err.h"
#include "button_event.h"

typedef void (*gpio_manager_button_event_cb_t)(button_event_t event, void *ctx);

esp_err_t gpio_manager_init(gpio_manager_button_event_cb_t cb, void *ctx);
