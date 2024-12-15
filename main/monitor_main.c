#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "esp_wifi.h"
#include <sys/socket.h>
#include <netdb.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "wifi_station.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "driver/i2c.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "wifi_ap.h"
#include "mqtt_publisher.h"
#include "bmp280.h"
#include "http_server.h"
#include "i2c_driver.h"

#include "esp_log.h"
#include <esp_timer.h>

#define BLINK_GPIO 2
#define BUTTON_GPIO_PIN 0

static bool led_state = false;  // stan diody ON/OFF
bool is_config_mode = false;
extern httpd_handle_t server;

static esp_timer_handle_t button_timer; // Timer do obsługi timeout dla kliknięć
static uint32_t click_count = 0;        // Licznik kliknięć
static const uint32_t double_click_timeout_ms = 500; // 500 ms na podwójne kliknięcie

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

/* Task migania diodą */ 
void led_blink_task(void *pvParameter)
{
    while (1) {
        if (is_config_mode) {
            gpio_set_level(BLINK_GPIO, 1); // stałe światło
            vTaskDelay(500 / portTICK_PERIOD_MS);
        } else if (!wifi_connected) {
            gpio_set_level(BLINK_GPIO, led_state);
            led_state = !led_state;
            vTaskDelay(500 / portTICK_PERIOD_MS);  // Miga co 0.5 s
        } else {
            gpio_set_level(BLINK_GPIO, 0); // Wyłączona dioda gdy jest połączenie
            vTaskDelay(1000 / portTICK_PERIOD_MS);  // Sprawdzaj co 1 s
        }
    }
}

/* Wykrywanie kliknięć przycisku (pojedyncze lub podwójne) */
void button_timer_callback(void* arg) {
    uint32_t clicks;

    portENTER_CRITICAL_ISR(&mux); // Synchronizacja 
    clicks = click_count;
    click_count = 0; // Zresetuj licznik
    portEXIT_CRITICAL_ISR(&mux);

    if (clicks == 1) {
        // Obsługa pojedynczego kliknięcia
        float temperature, pressure;
        while (bmp280_is_measuring()) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        bmp280_read_data(&temperature, &pressure);

        ESP_LOGI("BMP280", "Temperatura: %.2f °C, Ciśnienie: %.2f hPa", temperature, pressure / 100.0);

        // Publikacja MQTT
        if (!is_config_mode && wifi_connected) {
            publish_data(client_handle,"user1", "device1");

        }
    } else if (clicks == 2) {
        // Obsługa podwójnego kliknięcia
        notify_clients(server, "{\"mode\": \"AP\"}");
        if (!is_config_mode) {
            is_config_mode = true;
            mqtt_stop();
            if (server != NULL) stop_webserver(server);

            ESP_LOGI("CONFIG", "Przełączanie do trybu konfiguracji...");
            gpio_set_level(BLINK_GPIO, 1);
            esp_wifi_stop();
            vTaskDelay(pdMS_TO_TICKS(500));
            wifi_init_ap();
            server = start_webserver();
        }
    }
}

/* Nasłuchuje na powiadomienia z systemu i przełącza między trybem AP i STA */
void config_mode_task(void* arg) {
    uint32_t notify_value;
    while (1) {
        // Oczekiwanie na powiadomienie
        if (xTaskNotifyWait(0, ULONG_MAX, &notify_value, portMAX_DELAY) == pdTRUE) {
            //ESP_LOGI("CONFIG", "Otrzymano powiadomienie: %u", notify_value);

            if (notify_value == 1) { // Przejście do trybu konfiguracji
                notify_clients(server, "{\"mode\": \"AP\"}");
                if (!is_config_mode) {
                    is_config_mode = true;
                    mqtt_stop();
                    if (server != NULL) {
                        stop_webserver(server);
                    }
                    ESP_LOGI("CONFIG", "Przełączanie do trybu konfiguracji...");
                    gpio_set_level(BLINK_GPIO, 1);
                    esp_wifi_stop();
                    vTaskDelay(pdMS_TO_TICKS(500));
                    wifi_init_ap();
                    server = start_webserver();
    
                }
            } else if (notify_value == 2) { // Przejście do trybu station
                notify_clients(server, "{\"mode\": \"STA\"}");
                if (is_config_mode) {
                    is_config_mode = false;
                    mqtt_stop();
                    ESP_LOGI("CONFIG", "MQTT zatrzymane przed przejściem do STA.");
                    ESP_LOGI("CONFIG", "Przełączanie do trybu pracy (Station)...");
                    gpio_set_level(BLINK_GPIO, 0);
                    if (server != NULL) {
                        stop_webserver(server);
                    }
                    esp_wifi_stop();
                    ESP_LOGI("CONFIG", "Wi-Fi zatrzymane.");
                    vTaskDelay(pdMS_TO_TICKS(500));

                    ESP_LOGI("CONFIG", "Niszczenie interfejsu AP...");
                    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
                    if (ap_netif) {
                        esp_netif_destroy(ap_netif);
                        ESP_LOGI("CONFIG", "Interfejs AP zniszczony.");
                    }

                    wifi_init_sta();
                    vTaskDelay(pdMS_TO_TICKS(200));
                    connect_to_wifi();
                    vTaskDelay(pdMS_TO_TICKS(200));
                    mqtt_initialize();
                }
            }
        }
    }
}

/* Konfigurowanie pinu do diody */
static void configure_led(void)
{
    gpio_reset_pin(BLINK_GPIO);  // Reset pinu
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);  // Ustawienie pinu jako wyjście  
}

TaskHandle_t config_task_handle = NULL;

/* Obsługa przerwania dla przycisku */
static void IRAM_ATTR button_isr_handler(void* arg) {
    static uint32_t last_interrupt_time = 0;
    uint32_t current_interrupt_time = xTaskGetTickCountFromISR();

    if ((current_interrupt_time - last_interrupt_time) > pdMS_TO_TICKS(200)) { // przynajmniej 200 ms
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        portENTER_CRITICAL_ISR(&mux); // Synchronizacja
        click_count++;
        portEXIT_CRITICAL_ISR(&mux);

        esp_timer_stop(button_timer);
        esp_timer_start_once(button_timer, double_click_timeout_ms * 1000); 
    }

    last_interrupt_time = current_interrupt_time;
}

/* Konfiguracja przycisku */
void configure_button() {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, // Zmiana z wysokiego na niski
        .mode = GPIO_MODE_INPUT,        // Pin jako wejście
        .pin_bit_mask = (1ULL << BUTTON_GPIO_PIN), // Przesunięcie bitu odpowiadającego pinowi przycisku
        .pull_down_en = 0,
        .pull_up_en = 1,        
    };
    gpio_config(&io_conf);

    // Inicjalizacja przerwań GPIO
    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3);
    gpio_isr_handler_add(BUTTON_GPIO_PIN, button_isr_handler, NULL);

    // Inicjalizacja timera
    const esp_timer_create_args_t timer_args = {
        .callback = &button_timer_callback,
        .name = "button_timer"
    };
    esp_timer_create(&timer_args, &button_timer);
}

bmp280_config_t bmp280_default_config = {
    .oversampling_temp = BMP280_OSRS_X1,
    .oversampling_press = BMP280_OSRS_X1,
    .standby_time = 0x05,
    .filter = 0x04,
    .mode = BMP280_SLEEP_MODE
};

void app_main(void)
{

    // Inicjalizacja NVS do przechowywania konfiguracji wifi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init()); // Inicjalizacja stosu TCP/IP    
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // Inicjalizacja systemu zdarzeń

    configure_button(); // Konfiguracja przycisku
    configure_led(); // Konfiguracja diody
    
    // Konfiguracja Wi-Fi w trybie STA
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));


    i2c_master_init();
    vTaskDelay(pdMS_TO_TICKS(100)); 

    
    bmp280_init();
    load_bmp280_config_from_nvs(&bmp280_default_config);
    bmp280_apply_config(&bmp280_default_config);

    // Uruchomienie tasków
    xTaskCreate(config_mode_task, "config_mode_task", 10240, NULL, 5, &config_task_handle);
    xTaskCreate(&led_blink_task, "led_blink_task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    initialize_mqtt_mutex();

    // Inicjalizacja Wi-Fi i innych modułów
    wifi_init_sta();
    connect_to_wifi();
    vTaskDelay(pdMS_TO_TICKS(500)); 
    server = start_webserver();
    notify_clients(server, "{\"mode\": \"STA\"}");

    ///


   /* i2c_master_init();
    vTaskDelay(pdMS_TO_TICKS(100)); 
    bmp280_init();

   while (1) {
        bmp280_read_data();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }*/
    
}