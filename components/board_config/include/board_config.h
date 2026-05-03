#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int board_config_system_led_gpio(void);
bool board_config_system_led_active_low(void);

size_t board_config_button_count(void);
int board_config_button_gpio(size_t index);
bool board_config_button_active_low(void);

/* Returns -1 when the board profile has no battery telemetry hardware. */
int board_config_battery_gpio(void);
/* Battery voltage = ADC pin voltage * numerator / denominator. */
uint32_t board_config_battery_divider_num(void);
uint32_t board_config_battery_divider_den(void);
