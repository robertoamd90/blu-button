idf_component_register(
    SRCS "system_runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES gpio_manager led_feedback device_identity ble_button_tx battery_telemetry
    PRIV_REQUIRES esp_hw_support freertos nvs_flash
)
