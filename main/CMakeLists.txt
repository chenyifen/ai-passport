idf_component_register(
    SRCS "phone_remote_main.c" "phone_remote_model.c" "phone_remote_ble.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_timer nvs_flash
)
