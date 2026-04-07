idf_component_register(
    SRCS "led_feedback.c"
    INCLUDE_DIRS "include"
    REQUIRES button_event
    PRIV_REQUIRES freertos esp_driver_gpio board_config
)
