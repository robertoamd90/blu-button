#pragma once

#include <stdbool.h>
#include <stddef.h>

int board_config_system_led_gpio(void);
bool board_config_system_led_active_low(void);

size_t board_config_button_count(void);
int board_config_button_gpio(size_t index);
bool board_config_button_active_low(void);
