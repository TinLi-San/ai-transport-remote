idf_component_register(
    SRCS "main.c"
         "media_cmd.c"
         "hid_consumer.c"
         "ble_hid_gap.c"
         "media_remote.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_hid esp_event esp_hw_support esp_timer nvs_flash
)
