#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "wifi_station.h"
#include "mqtt_publisher.h"
#include <string.h>
#include <esp_http_server.h>
#include <freertos/task.h>

#define BLINK_GPIO     GPIO_NUM_2  // nr pinu GPIO dla LED

static const char *TAG = "wifi_station";

static EventGroupHandle_t wifi_event_group; 
const int WIFI_CONNECTED_BIT = BIT0; // połączenie
const int WIFI_FAIL_BIT = BIT1; // nieudana próba

bool wifi_connected = false; 


/* Event handler dla zdarzeń wifi */
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) { // Tryb STATION się uruchomił
        if(!is_config_mode) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) { // Rozłączenie 
        wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGI(TAG, "Rozłączono z Wi-Fi. Powód: %d", event->reason);
        wifi_connected = false;
        ESP_LOGI(TAG, "Zatrzymuję MQTT...");
        mqtt_stop();


        if (event->reason == WIFI_REASON_AUTH_FAIL) {
            ESP_LOGE(TAG, "Nieprawidłowe hasło. Przerwanie prób łączenia.");
            return;
        }

        if (!is_config_mode) {
            ESP_LOGI(TAG, "Próba ponownego połączenia z siecią station...");
            esp_wifi_connect();
        }
        
    
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) { // Otrzymano adres ip
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        
        char ip_str[16]; 
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str)); // Konwersja adresu IP na łańcuch znaków
        ESP_LOGI(TAG, "Uzyskano IP: %s", ip_str);

        if (!wifi_connected) {  
            wifi_connected = true;
        
            ESP_LOGI(TAG, "Stan wifi_connected zmieniony na: %d", wifi_connected);
            vTaskDelay(pdMS_TO_TICKS(100));
            ESP_LOGI(TAG, "Połączono z Wi-Fi. Inicjalizacja MQTT...");
            mqtt_initialize();
        }
    }
}



void save_wifi_credentials_to_nvs(const char* ssid, const char* password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_data", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Błąd przy otwieraniu NVS");
        return;
    }

    nvs_set_str(nvs_handle, "ssid", ssid);
    nvs_set_str(nvs_handle, "password", password);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    ESP_LOGI("NVS", "Zapisano dane sieci Wi-Fi: SSID=%s", ssid);
}
void connect_to_wifi() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_data", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Błąd przy otwieraniu NVS. Sprawdź czy zapisano dane sieci Wi-Fi.");
        return;
    }

    char ssid[32], password[64];
    size_t ssid_len = sizeof(ssid);
    size_t password_len = sizeof(password);

    if (nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len) != ESP_OK ||
        nvs_get_str(nvs_handle, "password", password, &password_len) != ESP_OK) {
        ESP_LOGE(TAG, "Nie znaleziono danych Wi-Fi w NVS.");
        nvs_close(nvs_handle);
        return; 
    }
    nvs_close(nvs_handle);


    if (strlen(ssid) > 31 || strlen(password) > 63) {
        ESP_LOGE(TAG, "Nieprawidłowa długość SSID (%zu) lub hasła (%zu).", strlen(ssid), strlen(password));
        return;
    }

    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_LOGI(TAG, "Łączenie do Wi-Fi: SSID=%s", ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Konfiguracja Wi-Fi zakończona.");
}

void wifi_init_sta(void)
{

    // Zresetuj interfejs sieciowy dla STA
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_destroy(netif);
        ESP_LOGI("wifi_sta", "Interfejs WIFI_STA_DEF został zniszczony.");
    }


    wifi_event_group = xEventGroupCreate(); // Grupa zdarzeń do śledzenia stanu połączenia

    
    esp_netif_create_default_wifi_sta(); // domyślny interfejs sieciowy


    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    // Rejestracja event handlerów dla zdarzeń WiFi i IP
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

}


