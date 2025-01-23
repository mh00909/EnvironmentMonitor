idf_component_register(SRCS "monitor_main.c" "ble_sensor.c" "wifi_station.c" "http_server.c" "wifi_ap.c" "mqtt_publisher.c" 
                       INCLUDE_DIRS "."
                       REQUIRES bt sensor_handler bmp280 esp_http_server driver esp_wifi esp_http_client driver nvs_flash hal freertos spiffs mqtt esp_http_server app_update)
