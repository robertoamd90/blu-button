#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "button_event.h"

/* Starts or reuses the broadcaster stack so startup can overlap other wake work. */
esp_err_t ble_button_tx_init(const uint8_t key[16]);
/* Requires successful init() in the current wake cycle and owns send-time recovery. */
esp_err_t ble_button_tx_send_event(button_event_t event);
