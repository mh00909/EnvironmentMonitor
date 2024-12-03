#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "wifi_station.h"
#include "esp_http_server.h"
#include "wifi_ap.h"
#include <string.h>

#define AP_SSID "ESP32-AP"
#define AP_PASS "12345678"
#define MAX_STA_CONN 4 // max liczba klientów

static const char *TAG = "wifi_ap"; // do logowania wiadomości

httpd_handle_t server = NULL;

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
static esp_err_t handle_confirm_wifi(httpd_req_t *req) {
    ESP_LOGI("HTTP", "Potwierdzenie połączenia w trybie AP");
    xTaskNotify(config_task_handle, 2, eSetValueWithoutOverwrite); // Przełącz na tryb STA
    httpd_resp_send(req, "ESP32 przełącza się na tryb STA", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
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
        ESP_LOGI("HTTP", "Received SSID: %s, Password: %s", ssid, password);

        // Zapisz dane Wi-Fi do pamięci (NVS)
        save_wifi_credentials_to_nvs(ssid, password);
        // Powiadomienie o przełączeniu na tryb Station
        ESP_LOGI("HTTP", "Powiadamianie o przejściu do trybu Station...");
        xTaskNotify(config_task_handle, 2, eSetValueWithoutOverwrite);

        // Wyślij odpowiedź do klienta
        httpd_resp_send(req, "Zaaktualizowano dane sieci Wi-Fi", HTTPD_RESP_USE_STRLEN);

        // Połącz z Wi-Fi w osobnym tasku
      //  connect_to_wifi();

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

void register_endpoints(httpd_handle_t server) {

    httpd_uri_t wifi_config_endpoint = {
        .uri       = "/wifi_config",
        .method    = HTTP_GET,
        .handler   = handle_wifi_config,
        .user_ctx  = NULL
    };

    httpd_register_uri_handler(server, &wifi_config_endpoint);

    httpd_uri_t wifi_post = {
        .uri = "/set_wifi",
        .method = HTTP_POST,
        .handler = handle_set_wifi_post,
        .user_ctx = NULL,
    };

 httpd_uri_t confirm_wifi_endpoint = {
        .uri       = "/confirm_wifi",
        .method    = HTTP_GET,
        .handler   = handle_confirm_wifi,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &confirm_wifi_endpoint);
    esp_err_t err = httpd_register_uri_handler(server, &wifi_post);
    if (err == ESP_OK) {
        ESP_LOGI("HTTP", "Zarejestrowano endpoint: /set_wifi");
    } else {
        ESP_LOGE("HTTP", "Błąd przy rejestracji endpointu /set_wifi: %s", esp_err_to_name(err));
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
            ESP_LOGI("HTTP", "Serwer zatrzymany");
        } else {
            ESP_LOGE("HTTP", "Błąd przy zatrzymywaniu serwera: %s", esp_err_to_name(err));
        }
        server = NULL; 
    } else {
        ESP_LOGW("HTTP", "Serwer już został zatrzymany.");
    }
}



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
