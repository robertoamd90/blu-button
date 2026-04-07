#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "button_event.h"

esp_err_t ble_button_tx_init(const uint8_t key[16]);
esp_err_t ble_button_tx_send_event(button_event_t event);
