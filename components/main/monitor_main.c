#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "wifi_station.h"
#include "esp_mac.h"


#define BLINK_GPIO 2

static bool led_state = false;  // stan diody ON/OFF

// task migania diodą
void led_blink_task(void *pvParameter)
{
    while (1) {
        if (!wifi_connected) {
            gpio_set_level(BLINK_GPIO, led_state);
            led_state = !led_state;
            vTaskDelay(500 / portTICK_PERIOD_MS);  // Miga co 0.5 s
        } else {
            gpio_set_level(BLINK_GPIO, 1); // Świeci stale
            vTaskDelay(1000 / portTICK_PERIOD_MS);  // Sprawdzaj co 1 s
        }
    }
}

// Konfiguracja pinu GPIO jako wyjścia do sterowania diodą
static void configure_led(void)
{
    gpio_reset_pin(BLINK_GPIO);  // Reset pinu
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);  // Ustawienie pinu jako wyjście
    
}


void app_main(void)
{
    configure_led();  // Skonfiguruj pin GPIO
    wifi_init_sta(); // Nawiąż połączenie wifi

    xTaskCreate(&led_blink_task, "led_blink_task", configMINIMAL_STACK_SIZE, NULL, 5, NULL); // Uruchom task do migania diodą


}
