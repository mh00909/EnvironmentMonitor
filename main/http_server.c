#include "http_server.h"
#include "esp_http_server.h"
#include "wifi_station.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "wifi_ap.h"
#include "freertos/FreeRTOS.h"
#include "esp_http_server.h"

/*
- Obsługa żądań HTTP i WebSocket
- Endpointy do konfiguracji i komunikacji
- Zarządzanie stanem serwera
*/

static const char *TAG = "HTTP_SERVER";

httpd_handle_t server = NULL;

// Maksymalna liczba klientów, których można obsłużyć
#define MAX_CLIENTS 5
static int client_sockets[MAX_CLIENTS] = {-1}; // Tablica uchwytów klientów

void register_client(int sockfd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] == -1) { // Znajdź wolne miejsce w tablicy
            client_sockets[i] = sockfd;
            ESP_LOGI(TAG, "Registered client socket: %d", sockfd);
            return;
        }
    }
    ESP_LOGW(TAG, "Max clients reached. Could not register socket: %d", sockfd);
}

void unregister_client(int sockfd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] == sockfd) {
            client_sockets[i] = -1; // Usuń klienta z listy
            ESP_LOGI(TAG, "Unregistered client socket: %d", sockfd);
            return;
        }
    }
    ESP_LOGW(TAG, "Socket %d not found in client list", sockfd);
}

void notify_clients(httpd_handle_t server, const char *message) {
    esp_err_t err;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t)); // Wyzeruj strukturę
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;            // Typ wiadomości WebSocket
    ws_pkt.payload = (uint8_t *)message;         // Ustaw treść wiadomości
    ws_pkt.len = strlen(message);                // Długość wiadomości

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] != -1) { // Sprawdź, czy klient jest zarejestrowany
            err = httpd_ws_send_frame_async(server, client_sockets[i], &ws_pkt); // Dodano uchwyt serwera
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Sent message to client %d: %s", client_sockets[i], message);
            } else {
                ESP_LOGE(TAG, "Failed to send message to client %d: %s", client_sockets[i], esp_err_to_name(err));
                unregister_client(client_sockets[i]); // Usuń klienta w przypadku błędu
            }
        }
    }
}



httpd_handle_t start_webserver(void) {
    if (server != NULL) {
        ESP_LOGW("HTTP", "Serwer już działa, pominięcie inicjalizacji.");
        return server; // Jeśli serwer już działa, po prostu zwróć uchwyt
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;           // Zwiększ stos
    config.max_uri_handlers = 5;       // Liczba obsługiwanych URI
    config.recv_wait_timeout = 10;     // Timeout na odbiór danych (sekundy)
    config.send_wait_timeout = 10;     // Timeout na wysyłanie danych (sekundy)
    config.lru_purge_enable = true;    // Automatyczne zwalnianie zasobów
    config.max_open_sockets = 5;

    config.lru_purge_enable = true; // Usuwanie najstarszych połączeń
    config.recv_wait_timeout = 5;   // 5 sekund na odbiór danych
    config.send_wait_timeout = 5;   // 5 sekund na wysłanie danych

    
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI("HTTP", "Serwer uruchomiony pomyślnie.");
        register_endpoints(server);
        return server;
    }


    ESP_LOGE("HTTP", "Błąd przy uruchamianiu serwera.");
    return NULL;
}

void stop_webserver(httpd_handle_t server) {
    if (server != NULL) {
        esp_err_t err = httpd_stop(server);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Serwer HTTP zatrzymany poprawnie.");
        } else {
            ESP_LOGE(TAG, "Błąd przy zatrzymywaniu serwera HTTP: %s", esp_err_to_name(err));
        }
        server = NULL;
    } else {
        ESP_LOGW(TAG, "Serwer już został zatrzymany.");
    }
}

// Funkcja obsługi WebSocket
esp_err_t websocket_handler(httpd_req_t *req) {
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    // Odbierz ramkę WebSocket
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error receiving WebSocket frame: %s", esp_err_to_name(ret));
        return ret;
    }

    // Sprawdź typ ramki
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        ESP_LOGI(TAG, "Received WebSocket TEXT frame");
        ws_pkt.payload = (uint8_t *)"Pong!";
        ws_pkt.len = strlen("Pong!");
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;
        return httpd_ws_send_frame(req, &ws_pkt);
    } else {
        ESP_LOGW(TAG, "Unsupported WebSocket frame type");
        return ESP_FAIL;
    }
}


static esp_err_t handle_confirm_wifi(httpd_req_t *req) {
    ESP_LOGI("HTTP", "Potwierdzenie połączenia w trybie AP");
    xTaskNotify(config_task_handle, 2, eSetValueWithoutOverwrite); // Przełącz na tryb STA
    httpd_resp_send(req, "ESP32 przełącza się na tryb STA", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handle_wifi_config(httpd_req_t *req) {
    char *response = "{\"mode\": \"unknown\"}";

    // Zwracaj tryb zależnie od flagi lub konfiguracji
    if (is_config_mode) {
        response = "{\"mode\": \"AP\"}";
    } else {
        response = "{\"mode\": \"STA\"}";
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handle_config(httpd_req_t *req) {
    const char *response = 
        "{"
        "\"wifi_config\": true,"
        "\"mqtt_config\": true,"
        "\"device_name\": \"ESP32-Monitor\","
        "\"firmware_version\": \"1.0.0\""
        "}";

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}



void register_endpoints(httpd_handle_t server) {

    httpd_uri_t ws_uri = {
        .uri       = "/ws",           // Endpoint WebSocket
        .method    = HTTP_GET,        // Obsługuj połączenia WebSocket
        .handler   = websocket_handler, 
        .is_websocket = true          // Flaga, aby rozpoznać jako WebSocket
    };
    httpd_register_uri_handler(server, &ws_uri);


    httpd_uri_t wifi_post = {
        .uri = "/set_wifi",
        .method = HTTP_POST,
        .handler = handle_set_wifi_post,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &wifi_post);

    
    httpd_uri_t config_page_endpoint = {
        .uri       = "/config",
        .method    = HTTP_GET,
        .handler   = handle_config,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &config_page_endpoint);

    
    httpd_uri_t wifi_config_endpoint = {
        .uri       = "/wifi_config",
        .method    = HTTP_GET,
        .handler   = handle_wifi_config,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &wifi_config_endpoint);


}










esp_err_t handle_set_wifi_post(httpd_req_t *req) {
    ESP_LOGI("HTTP", "Rozpoczęto obsługę żądania POST na /set_wifi");
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1); // Odbierz dane
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req); // Wyślij błąd Timeout
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0'; // Dodaj terminator końca ciągu

    char ssid[32], password[64];
    ESP_LOGI("HTTP", "Odebrano dane: %s", buf);
    if (sscanf(buf, "ssid=%31[^&]&password=%63s", ssid, password) == 2) {
        ESP_LOGI("HTTP", "Otrzymano SSID: %s, Password: %s", ssid, password);

        // Zapisz dane Wi-Fi do pamięci (NVS)
        save_wifi_credentials_to_nvs(ssid, password);
        // Powiadomienie o przełączeniu na tryb Station
        const char *html_response = 
            "<html>"
            "<head>"
            "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }</style>"
            "</head>"
            "<body>"
            "<h1>Dane Wi-Fi zapisane.</h1>"
            "<p>Zamknij okno.</p>"
            "</body>"
            "</html>";


        char response[512];
        snprintf(response, sizeof(response), html_response, ssid);
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);

        // Powiadomienie o przełączeniu na tryb Station
        xTaskNotify(config_task_handle, 2, eSetValueWithoutOverwrite);

        return ESP_OK;
    } else {
        ESP_LOGE("HTTP", "Błąd przy parsowaniu ssid i hasła");
        httpd_resp_send(req, "Zły format danych.", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
}
esp_err_t handle_switch_to_config(httpd_req_t *req) {
    xTaskNotify(config_task_handle, 1, eSetValueWithoutOverwrite); // Przełączenie do trybu AP

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"message\":\"Przełączono do trybu konfiguracji. Połącz się z siecią ESP32.\"}");
    return ESP_OK;
}

esp_err_t handle_switch_to_station(httpd_req_t *req) {
    xTaskNotify(config_task_handle, 2, eSetValueWithoutOverwrite); // Wywołaj przejście do trybu station
    httpd_resp_send(req, "Przełączono do trybu pracy (Station)", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
