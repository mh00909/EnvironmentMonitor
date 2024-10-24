idf_component_register(SRCS "monitor_main.c" "wifi_station.c" "http_request.c" "wifi_ap.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_http_client driver nvs_flash hal freertos)
