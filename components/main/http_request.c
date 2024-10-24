#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "http_request.h"
#include "wifi_station.h"

static const char *REQUEST = "GET / HTTP/1.1\r\n"
                             "Host: " SERVER_HOST "\r\n"
                             "Connection: close\r\n"
                             "\r\n";

/* Task do wykonania żądania http get */
void http_get_task(void *pvParameters) {
    char recv_buf[1024];
    struct sockaddr_in dest_addr;
    struct addrinfo hints, *res;
    int err;

    // Pętla sprawdzająca połączenie Wi-Fi
    while (!wifi_connected) {
        ESP_LOGI("HTTP", "Brak połączenia z Wi-Fi. Oczekiwanie...");
        vTaskDelay(5000 / portTICK_PERIOD_MS);  // Poczekaj 5 sekund
    }

    ESP_LOGI("HTTP", "Połączono z Wi-Fi, rozpoczynam zapytanie HTTP...");

     // Zdefiniowanie właściwości połączenia dla DNS lookup
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; 
    hints.ai_socktype = SOCK_STREAM;

    // DNS lookup
    err = getaddrinfo(SERVER_HOST, "80", &hints, &res);  
    if (err != 0 || res == NULL) {
        ESP_LOGE("HTTP", "Błąd DNS lookup: %d", err);
        vTaskDelete(NULL);
        return;
    }


    dest_addr.sin_addr.s_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(SERVER_PORT);

    freeaddrinfo(res); 

    // Tworzenie gniazda TCP
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE("HTTP", "Błąd utworzenia gniazda: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI("HTTP", "Łączenie z %s:%d", SERVER_HOST, SERVER_PORT);
    err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
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
            recv_buf[len] = 0;  
            printf("%s", recv_buf);
        }
    } while (len > 0);

    // Zamknięcie gniazda
    close(sock);
    ESP_LOGI("HTTP", "Połączenie zamknięte");
    vTaskDelete(NULL);
}
