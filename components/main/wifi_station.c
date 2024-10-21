#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/gpio.h"

#define WIFI_SSID      "UPCB797E35"
#define WIFI_PASS      "bb5zhdevFYye"
#define BLINK_GPIO     GPIO_NUM_2  // nr pinu GPIO dla LED

static const char *TAG = "wifi_station"; // do logowania wiadomości

static EventGroupHandle_t wifi_event_group; 

const int WIFI_CONNECTED_BIT = BIT0; // połączenie
const int WIFI_FAIL_BIT = BIT1; // nieudana próba

bool wifi_connected = false; 

/* Event handler dla zdarzeń WiFi */
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) { // startuje w trybie STATION - próbuje połączyć się z siecią
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) { // połączenie zerwane
      
        esp_wifi_connect();
        ESP_LOGI(TAG, "Ponowne łączenie do WiFi...");
        wifi_connected = false;  // Brak połączenia
        gpio_set_level(BLINK_GPIO, 0); // LED wyłączona, brak połączenia
    
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) { // Otrzymuje adres ip
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        
        char ip_str[16];  // Buffer na adres IP 
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "Uzyskano IP: %s", ip_str);

        wifi_connected = true;  // Połączenie udane
        gpio_set_level(BLINK_GPIO, 1); // LED włączona, połączenie udane
    }
}

void wifi_init_sta(void)
{
    wifi_event_group = xEventGroupCreate();

    
    ESP_ERROR_CHECK(nvs_flash_init()); // Inicjalizacja NVS - przechowuje dane konfiguracyjne
    
    ESP_ERROR_CHECK(esp_netif_init()); // Inicjalizacja stosu TCP/IP

    
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // Inicjalizacja systemu zdarzeń

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

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
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Inicjalizacja WiFi zakończona.");
}

