idf_component_register(
    SRCS "battery_telemetry.c"
    INCLUDE_DIRS "include"
    REQUIRES board_config
    PRIV_REQUIRES driver esp_adc
)
