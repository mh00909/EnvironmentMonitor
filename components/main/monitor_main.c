#include <stdio.h>
#include <string.h>
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
#include "lwip/sockets.h"
#include "lwip/inet.h"



#define BLINK_GPIO 2

static bool led_state = false;  // stan diody ON/OFF

// Adres serwera i port
#define SERVER_HOST    "93.184.216.34"
#define SERVER_PORT    80

// Zapytanie HTTP GET o stronę główną
static const char *REQUEST = "GET / HTTP/1.1\r\n"
                             "Host: " SERVER_HOST "\r\n"
                             "Connection: close\r\n"
                             "\r\n";


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
void http_get_task(void *pvParameters) {
    char recv_buf[1024];
    struct sockaddr_in dest_addr;

    // Pętla sprawdzająca połączenie WiFi
    while (!wifi_connected) {
        ESP_LOGI("HTTP", "Brak połączenia z Wi-Fi. Oczekiwanie...");
        vTaskDelay(5000 / portTICK_PERIOD_MS);  // Poczekaj 5 sekund
    }
    
    ESP_LOGI("HTTP", "Połączono z Wi-Fi, rozpoczynam zapytanie HTTP...");

    // Zdefiniowanie adresu serwera
    dest_addr.sin_addr.s_addr = inet_addr(SERVER_HOST);  
    dest_addr.sin_family = AF_INET;  // IPv4
    dest_addr.sin_port = htons(SERVER_PORT);  

    // Inicjalizacja gniazda TCP
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE("HTTP", "Błąd utworzenia gniazda: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI("HTTP", "Łączenie z %s:%d", SERVER_HOST, SERVER_PORT);
    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE("HTTP", "Błąd połączenia: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    // Wysłanie zapytania HTTP GET
    int sent = send(sock, REQUEST, strlen(REQUEST), 0);
    if (sent < 0) {
        ESP_LOGE("HTTP", "Błąd wysyłania zapytania: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI("HTTP", "Otrzymywanie odpowiedzi...");

    // Odbieranie odpowiedzi
    int len;
    do {
        len = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);
        if (len > 0) {
            recv_buf[len] = 0; // Null-terminate
            printf("%s", recv_buf);
        }
    } while (len > 0);

    // Zamknięcie gniazda
    close(sock);
    ESP_LOGI("HTTP", "Połączenie zamknięte");
    vTaskDelete(NULL);
}

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

    // Utworzenie zadania HTTP GET
    xTaskCreate(&http_get_task, "http_get_task", 8192, NULL, 5, NULL);
}
