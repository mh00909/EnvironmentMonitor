idf_component_register(SRCS "monitor_main.c" "wifi_station.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_http_client driver nvs_flash hal freertos)
