#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
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
#include "wifi_ap.c"
#include "http_request.h"
#include "wifi_ap.h"
#include "mqtt_publisher.h"
#include "bmp280_sensor.h"

#include "esp_log.h"

#define BLINK_GPIO 2

static bool led_state = false;  // stan diody ON/OFF


/* Task migania diodą */ 
void led_blink_task(void *pvParameter)
{
    while (1) {
        if (!wifi_connected) {
            gpio_set_level(BLINK_GPIO, led_state);
            led_state = !led_state;
            vTaskDelay(500 / portTICK_PERIOD_MS);  // Miga co 0.5 s
        } else {
            gpio_set_level(BLINK_GPIO, 0); // Wyłączona dioda gdy jest połączenie
            vTaskDelay(1000 / portTICK_PERIOD_MS);  // Sprawdzaj co 1 s
        }
    }
}

/* Konfigurowanie pinu do diody */
static void configure_led(void)
{
    gpio_reset_pin(BLINK_GPIO);  // Reset pinu
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);  // Ustawienie pinu jako wyjście  
}
#define I2C_MASTER_NUM I2C_NUM_0  // Używamy pierwszego portu I2C





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

    // Inicjalizacja Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));  // Ustawienie trybu STA



    configure_led(); 


    // Inicjalizacja STATION 
    wifi_init_sta();
   //  ble_server_init();

    xTaskCreate(&led_blink_task, "led_blink_task", configMINIMAL_STACK_SIZE, NULL, 1, NULL); // Uruchom task do migania diodą

   // xTaskCreate(&http_get_task, "http_get_task", 8192, NULL, 2, NULL); // Task do żądania http
    mqtt_initialize(); 

    
}
