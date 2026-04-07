idf_component_register(
    SRCS "ble_button_tx.c"
    INCLUDE_DIRS "include"
    REQUIRES button_event
    PRIV_REQUIRES bt mbedtls nvs_flash esp_timer
)
