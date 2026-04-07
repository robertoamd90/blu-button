#include "sdkconfig.h"
#include "board_config.h"

#if CONFIG_BB_BOARD_ESP32_DEVKIT_V1

int board_config_system_led_gpio(void)
{
    return 2;
}

bool board_config_system_led_active_low(void)
{
    return false;
}

int board_config_boot_button_gpio(void)
{
    return 0;
}

bool board_config_boot_button_active_low(void)
{
    return true;
}

#elif CONFIG_BB_BOARD_ESP32C3_SUPERMINI

int board_config_system_led_gpio(void)
{
    return 8;
}

bool board_config_system_led_active_low(void)
{
    return true;
}

int board_config_boot_button_gpio(void)
{
    return 9;
}

bool board_config_boot_button_active_low(void)
{
    return true;
}

#else
#error "Unsupported BluButton board profile"
#endif
