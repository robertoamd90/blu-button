#pragma once

#include <stdbool.h>

int board_config_system_led_gpio(void);
bool board_config_system_led_active_low(void);

int board_config_boot_button_gpio(void);
bool board_config_boot_button_active_low(void);
