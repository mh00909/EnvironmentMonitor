#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include <string.h>

#define AP_SSID "ESP32-AP"
#define AP_PASS "12345678"
#define MAX_STA_CONN 4 // max liczba klientów

static const char *TAG = "wifi_ap"; // do logowania wiadomości

void wifi_init_ap(void)
{
    // Pobiera bieżący kanał trybu station
    uint8_t primary_channel;
    wifi_second_chan_t second_channel;
    esp_wifi_get_channel(&primary_channel, &second_channel);  

    esp_netif_create_default_wifi_ap();  // Tworzy interfejs AP

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASS,
            .max_connection = MAX_STA_CONN,
            .channel = primary_channel,  
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };

    if (strlen(AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;  // AP otwarty jeśli brak hasła
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config)); 
    ESP_LOGI(TAG, "WiFi AP uruchomione. SSID:%s password:%s", AP_SSID, AP_PASS);
}
