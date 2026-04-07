idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES system_runtime
    PRIV_REQUIRES nvs_flash
)
