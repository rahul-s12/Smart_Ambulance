idf_component_register(
    SRCS
        "main.c"
        "wifi.c"
        "mqtt.c"
        "health_monitor.c"
        "oled_display.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_event
        esp_netif
        esp_wifi
        mqtt
        esp_adc
        driver
        nvs_flash
)
