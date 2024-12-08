#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "wifi_station.h"
#include "http_server.h"
#include "esp_http_server.h"
#include "wifi_ap.h"
#include <string.h>

#define AP_SSID "ESP32-AP"
#define AP_PASS "12345678"
#define MAX_STA_CONN 4 // max liczba klientów
#define CONFIG_HTTPD_MAX_OPEN_SOCKETS 3


/*
- Zarządzanie trybem AP
- Logika związana z Wi0Fi
- Powiązanie Wi-Fi z serwerem HTTP
*/

static const char *TAG = "wifi_ap"; // do logowania wiadomości

extern httpd_handle_t server;


void wifi_init_ap(void) {
    if (server != NULL) {
        ESP_LOGI("wifi_ap", "Restartowanie serwera HTTP.");
        stop_webserver(server);
        server = NULL;
    }


    // Zresetuj interfejs sieciowy dla AP
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (netif) {
        esp_netif_destroy(netif);
        ESP_LOGI("wifi_ap", "Interfejs WIFI_AP_DEF został zniszczony.");
    }

    // Inicjalizacja Wi-Fi w trybie AP
    esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASS,
            .max_connection = MAX_STA_CONN,
            .channel = 1,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };

    if (strlen(AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI("wifi_ap", "Uruchomiono Wi-Fi AP. SSID: %s, Password: %s", AP_SSID, AP_PASS);
    vTaskDelay(pdMS_TO_TICKS(100)); 

    // Sprawdź i uruchom serwer HTTP
    server = start_webserver();
    if (server == NULL) {
        ESP_LOGE("wifi_ap", "Błąd przy uruchamianiu serwera.");
    }
}
