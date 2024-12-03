idf_component_register(SRCS "monitor_main.c" "wifi_station.c" "http_request.c" "wifi_ap.c" "mqtt_publisher.c" "bmp280.c" "i2c_driver.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_wifi esp_http_client driver nvs_flash hal freertos spiffs mqtt esp_http_server)
