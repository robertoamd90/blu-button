#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "button_event.h"

#define BLE_BUTTON_TX_MAX_BUTTONS 6

/* Starts or reuses the broadcaster stack so startup can overlap other wake work. */
esp_err_t ble_button_tx_init(const uint8_t key[16]);
/* Encodes total_buttons BTHome button objects and marks active_button (1-based) with event. */
esp_err_t ble_button_tx_send_event(button_event_t event,
                                   size_t active_button,
                                   size_t total_buttons);
/* Waits until the current advertising session completes or the local adv deadline is enforced. */
esp_err_t ble_button_tx_wait_for_adv_complete(void);
