idf_component_register(
    SRCS "gpio_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES button_event
    PRIV_REQUIRES freertos esp_driver_gpio esp_hw_support esp_timer board_config
)
