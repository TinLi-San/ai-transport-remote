idf_component_register(
    SRCS "main.c"
         "media_cmd.c"
         "hid_consumer.c"
         "ble_hid_gap.c"
         "media_remote.c"
         "power_policy.c"
         "power_sleep.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_hid esp_event esp_hw_support esp_timer nvs_flash
             esp_pm esp_driver_gpio esp_driver_usb_serial_jtag
)
