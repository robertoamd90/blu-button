#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t device_identity_init(void);
const uint8_t *device_identity_get_key(void);
esp_err_t device_identity_get_mac(uint8_t out_mac[6]);
bool device_identity_key_was_created(void);
void device_identity_format_key_hex(char out[33]);
