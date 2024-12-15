#include "http_server.h"
#include "esp_http_server.h"
#include "wifi_station.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "wifi_ap.h"
#include "freertos/FreeRTOS.h"
#include "esp_http_server.h"
#include "bmp280.h"
#include "../../v5.3.1/esp-idf/components/json/cJSON/cJSON.h"
#include "nvs.h"


/*
- Obsługa żądań HTTP i WebSocket
- Endpointy do konfiguracji i komunikacji
- Zarządzanie stanem serwera
*/

static const char *TAG = "HTTP_SERVER";

httpd_handle_t server = NULL;

#define MAX_CLIENTS 5
static int client_sockets[MAX_CLIENTS] = {-1}; // Tablica uchwytów klientów

void register_client(int sockfd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] == -1) { // Znajdź wolne miejsce w tablicy
            client_sockets[i] = sockfd;
            ESP_LOGI(TAG, "Zarejstrowano klienta: %d", sockfd);
            return;
        }
    }
}

void unregister_client(int sockfd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] == sockfd) {
            client_sockets[i] = -1; // Usuń klienta z listy
            ESP_LOGI(TAG, "Usunięto klienta: %d", sockfd);
            return;
        }
    }
    ESP_LOGW(TAG, "Nie znaleziono %d w liście", sockfd);
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
                ESP_LOGI(TAG, "Wysłano wiadomość doo klienta %d: %s", client_sockets[i], message);
            } else {
                unregister_client(client_sockets[i]); // Usuń klienta w przypadku błędu
            }
        }
    }
}



httpd_handle_t start_webserver(void) {
    if (server != NULL) {
        return server; // Jeśli serwer już działa, po prostu zwróć uchwyt
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;           
    config.max_uri_handlers = 10;      
    config.recv_wait_timeout = 10;     // Timeout na odbiór danych 
    config.send_wait_timeout = 10;     // Timeout na wysyłanie danych
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
    } 
}

// Funkcja do testowania obsługi połączeń WebSocket
esp_err_t websocket_handler(httpd_req_t *req) {
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    // Odbierz ramkę WebSocket
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Błąd przy otrzymywaniu ramki WebSocket: %s", esp_err_to_name(ret));
        return ret;
    }
    // Sprawdź typ ramki
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        ws_pkt.payload = (uint8_t *)"Pong!";
        ws_pkt.len = strlen("Pong!");
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;
        return httpd_ws_send_frame(req, &ws_pkt);
    } else {
        return ESP_FAIL;
    }
}

/* Przejście do STA z AP */
static esp_err_t handle_confirm_wifi(httpd_req_t *req) {
    ESP_LOGI("HTTP", "Potwierdzenie połączenia w trybie AP");
    xTaskNotify(config_task_handle, 2, eSetValueWithoutOverwrite); // Przełącz na tryb STA
    httpd_resp_send(req, "ESP32 przełącza się na tryb STA", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* Sprawdzenie trybu pracy */
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

/* Do wysyłania informacji o urządzeniu */
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
        .uri       = "/ws",         
        .method    = HTTP_GET,       
        .handler   = websocket_handler, 
        .is_websocket = true       
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

    // Register OPTIONS handler for /bmp280_config
    httpd_uri_t bmp280_config_options = {
        .uri       = "/bmp280_config",
        .method    = HTTP_OPTIONS,
        .handler   = handle_options,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &bmp280_config_options);


    httpd_uri_t bmp280_config_get = {
        .uri       = "/bmp280_config",
        .method    = HTTP_GET,
        .handler   = handle_bmp280_config_get,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &bmp280_config_get);

    httpd_uri_t bmp280_config_post = {
        .uri       = "/bmp280_config",
        .method    = HTTP_POST,
        .handler   = handle_bmp280_config_post,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &bmp280_config_post);


    httpd_uri_t switch_to_sta_endpoint = {
        .uri       = "/switch_to_sta",
        .method    = HTTP_GET,
        .handler   = handle_switch_to_station,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &switch_to_sta_endpoint);

}










esp_err_t handle_set_wifi_post(httpd_req_t *req) {
    ESP_LOGI("HTTP", "Rozpoczęto obsługę żądania POST na /set_wifi");
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1); // Odbierz dane
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req); // błąd Timeout
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0'; 

    char ssid[32], password[64];
    ESP_LOGI("HTTP", "Odebrano dane: %s", buf);
    if (sscanf(buf, "ssid=%31[^&]&password=%63s", ssid, password) == 2) {
        ESP_LOGI("HTTP", "Otrzymano SSID: %s, Password: %s", ssid, password);

        // Zapisz dane Wi-Fi do pamięci 
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



static esp_err_t handle_options(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0); 
}



static esp_err_t handle_bmp280_config_get(httpd_req_t *req) {

    ESP_LOGI(TAG, "GET request received at /bmp280_config");
    char response[256];
    snprintf(response, sizeof(response),
             "{"
             "\"oversampling_temp\": %d,"
             "\"oversampling_press\": %d,"
             "\"standby_time\": %d,"
             "\"filter\": %d,"
             "\"mode\": %d"
             "}",
             bmp280_state.config.oversampling_temp,
             bmp280_state.config.oversampling_press,
             bmp280_state.config.standby_time,
             bmp280_state.config.filter,
             bmp280_get_mode());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


static esp_err_t handle_bmp280_config_post(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST request received at /bmp280_config");
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    ESP_LOGI(TAG, "Received POST data: %s", buf);

    // Parsowanie danych JSON
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    bmp280_config_t new_config = bmp280_state.config;

    cJSON *oversampling_temp = cJSON_GetObjectItem(root, "oversampling_temp");
    cJSON *oversampling_press = cJSON_GetObjectItem(root, "oversampling_press");
    cJSON *standby_time = cJSON_GetObjectItem(root, "standby_time");
    cJSON *filter = cJSON_GetObjectItem(root, "filter");
    cJSON *mode = cJSON_GetObjectItem(root, "mode");

    if (oversampling_temp && cJSON_IsNumber(oversampling_temp)) {
        new_config.oversampling_temp = oversampling_temp->valueint;
    }
    if (oversampling_press && cJSON_IsNumber(oversampling_press)) {
        new_config.oversampling_press = oversampling_press->valueint;
    }
    if (standby_time && cJSON_IsNumber(standby_time)) {
        new_config.standby_time = standby_time->valueint;
    }
    if (filter && cJSON_IsNumber(filter)) {
        new_config.filter = filter->valueint;
    }

    if (mode) {
        if (cJSON_IsString(mode)) {
            new_config.mode = (bmp280_mode_t)atoi(mode->valuestring);
        } else if (cJSON_IsNumber(mode)) {
            new_config.mode = (bmp280_mode_t)mode->valueint;
        } else {
            ESP_LOGE(TAG, "Nieprawidłowy format trybu pracy");
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid mode value");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Parsowanie JSON: tryb pracy = %d", new_config.mode);
    }

    // Zastosowanie konfiguracji
    if (bmp280_apply_config(&new_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply BMP280 config");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to apply config");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    bmp280_mode_t current_mode = bmp280_get_mode();
    ESP_LOGI(TAG, "Aktualny tryb pracy po konfiguracji: %d", current_mode);

    // Odpowiedź HTTP
    httpd_resp_send(req, "Zaaktualizowano konfigurację.", HTTPD_RESP_USE_STRLEN);

    // Zapis do NVS
    save_bmp280_config_to_nvs(&new_config);
    ESP_LOGI(TAG, "Zapis do NVS: mode=%d, temp=%d, press=%d, standby=%d, filter=%d",
             new_config.mode, new_config.oversampling_temp, new_config.oversampling_press,
             new_config.standby_time, new_config.filter);

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t handle_switch_to_station(httpd_req_t *req) {
    ESP_LOGI("HTTP_SERVER", "Przełączanie do trybu Station (STA)");

    // Powiadom task o przejściu na tryb STA
    xTaskNotify(config_task_handle, 2, eSetValueWithoutOverwrite);

    // Odpowiedź dla klienta
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"message\":\"ESP32 przełączono do trybu Station.\"}");

    return ESP_OK;
}


void save_bmp280_config_to_nvs(bmp280_config_t *config) {
    nvs_handle_t nvs_handle;
    if (nvs_open("bmp280", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_u8(nvs_handle, "oversampling_temp", config->oversampling_temp);
        nvs_set_u8(nvs_handle, "oversampling_press", config->oversampling_press);
        nvs_set_u8(nvs_handle, "standby_time", config->standby_time);
        nvs_set_u8(nvs_handle, "filter", config->filter);
        nvs_set_u8(nvs_handle, "mode", config->mode);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI("NVS", "Konfiguracja BMP280 zapisana.");
    } else {
        ESP_LOGE("NVS", "Błąd zapisu konfiguracji BMP280.");
    }
}

void load_bmp280_config_from_nvs(bmp280_config_t *config) {
    nvs_handle_t nvs_handle;
    if (nvs_open("bmp280", NVS_READONLY, &nvs_handle) == ESP_OK) {
        uint8_t value;
        if (nvs_get_u8(nvs_handle, "oversampling_temp", &value) == ESP_OK)
            config->oversampling_temp = value;

        if (nvs_get_u8(nvs_handle, "oversampling_press", &value) == ESP_OK)
            config->oversampling_press = value;

        if (nvs_get_u8(nvs_handle, "standby_time", &value) == ESP_OK)
            config->standby_time = value;

        if (nvs_get_u8(nvs_handle, "filter", &value) == ESP_OK)
            config->filter = value;

        if (nvs_get_u8(nvs_handle, "mode", &value) == ESP_OK)
            config->mode = value;

        nvs_close(nvs_handle);
        ESP_LOGI("NVS", "Konfiguracja BMP280 odczytana z NVS.");
    } else {
        ESP_LOGW("NVS", "Brak zapisanej konfiguracji BMP280. Użyto domyślnej konfiguracji.");
    }
}
