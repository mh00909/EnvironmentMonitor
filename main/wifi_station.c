#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/gpio.h"

#define WIFI_SSID      "test"
#define WIFI_PASS      "haslo123"
#define BLINK_GPIO     GPIO_NUM_2  // nr pinu GPIO dla LED

static const char *TAG = "wifi_station"; // do logów

static EventGroupHandle_t wifi_event_group; 

const int WIFI_CONNECTED_BIT = BIT0; // połączenie
const int WIFI_FAIL_BIT = BIT1; // nieudana próba

bool wifi_connected = false; 

/* Event handler dla zdarzeń wifi */
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) { // Tryb STATION się uruchomił
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) { // Rozłączenie 
      
        esp_wifi_connect();
        ESP_LOGI(TAG, "Brak połączenia z siecią Wi-Fi STATION. Próba połączenia...");
        
        
        if(wifi_connected) {
            wifi_connected = false;
            ESP_LOGI(TAG, "Stan wifi_connected zmieniony na: %d", wifi_connected); 
        }
        
    
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) { // Otrzymano adres ip
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        
        char ip_str[16]; 
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str)); // Konwersja adresu IP na łańcuch znaków
        ESP_LOGI(TAG, "Uzyskano IP: %s", ip_str);

        if (!wifi_connected) {  
            wifi_connected = true;
            ESP_LOGI(TAG, "Stan wifi_connected zmieniony na: %d", wifi_connected);
        }
    }
}

void wifi_init_sta(void)
{
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

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Inicjalizacja Wi-Fi w trybie STATION zakończona.");
}


