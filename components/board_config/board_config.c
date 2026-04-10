#include "sdkconfig.h"
#include "board_config.h"

#if CONFIG_BB_BOARD_ESP32_DEVKIT_V1

/*
 * Classic ESP32 deep-sleep wake uses RTC GPIOs only:
 *   GPIO0, GPIO2, GPIO4, GPIO12..15, GPIO25..27, GPIO32..39
 * Practical preference order for external buttons is:
 *   GPIO33, GPIO32, GPIO27, GPIO26, GPIO25, GPIO4
 * GPIO34..39 are input-only and do not have internal pull-ups/pull-downs.
 * GPIO0, GPIO2, GPIO12, and GPIO15 are strapping pins, and GPIO12..15 also
 * overlap JTAG, so keep them as later choices.
 * Multi-button active-low wake is not practical on this board profile. If
 * multiple GPIOs are listed here, only the first one will be used and the
 * runtime will expose a single logical button.
 */
static const int s_button_gpios[] = {0};

int board_config_system_led_gpio(void)
{
    return 2;
}

bool board_config_system_led_active_low(void)
{
    return false;
}

#elif CONFIG_BB_BOARD_ESP32C3_SUPERMINI

/*
 * ESP32-C3 deep-sleep wake is limited to GPIO0..5 on this board family.
 * Practical preference order for external buttons is:
 *   GPIO3, GPIO4, GPIO1, GPIO0, GPIO5, GPIO2
 * GPIO2 does work, but keep it as a last resort because it is a strapping pin.
 */
static const int s_button_gpios[] = {3,4,1,0,5,2};

int board_config_system_led_gpio(void)
{
    return 8;
}

bool board_config_system_led_active_low(void)
{
    return true;
}

#else
#error "Unsupported BluButton board profile"
#endif

size_t board_config_button_count(void)
{
    return sizeof(s_button_gpios) / sizeof(s_button_gpios[0]);
}

int board_config_button_gpio(size_t index)
{
    return index < board_config_button_count() ? s_button_gpios[index] : -1;
}

bool board_config_button_active_low(void)
{
    return true;
}
