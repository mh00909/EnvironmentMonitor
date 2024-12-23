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


static const char *TAG = "HTTP_SERVER";

httpd_handle_t server = NULL;


httpd_handle_t start_webserver(void) {
    if (server != NULL) {
        return server; 
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;           
    config.max_uri_handlers = 10;      
    config.recv_wait_timeout = 10;     // Timeout na odbiór danych 
    config.send_wait_timeout = 10;     // Timeout na wysyłanie danych
    config.lru_purge_enable = true;    // Automatyczne zwalnianie zasobów
    config.max_open_sockets = 5;

    config.lru_purge_enable = true; // Usuwanie najstarszych połączeń
    config.recv_wait_timeout = 5;  
    config.send_wait_timeout = 5;

    
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI("HTTP", "Serwer uruchomiony pomyślnie.");
        register_endpoints(server);
        return server;
    }

    return NULL;
}

void stop_webserver(httpd_handle_t server) {
    if (server != NULL) {
        esp_err_t err = httpd_stop(server);
    
        server = NULL;
    } 
}

/* Przejście do STA z AP */
static esp_err_t handle_confirm_wifi(httpd_req_t *req) {
    vTaskDelay(pdMS_TO_TICKS(2000));
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


    httpd_uri_t mqtt_post = {
        .uri = "/set_mqtt",
        .method = HTTP_POST,
        .handler = handle_set_mqtt_post,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &mqtt_post);

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



esp_err_t handle_bmp280_config_get(httpd_req_t *req) {

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


esp_err_t handle_bmp280_config_post(httpd_req_t *req) {
    
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
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Niepoprawna wartość");
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


esp_err_t handle_set_mqtt_post(httpd_req_t *req) {
    ESP_LOGI("HTTP", "Rozpoczęto obsługę żądania POST na /set_mqtt");
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1); // Odbierz dane
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req); // Wyślij błąd Timeout
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0'; // Dodaj terminator końca ciągu

    char broker[128], user[64], password[64];
    int port;
    if (sscanf(buf, "mqtt_broker=%127[^&]&mqtt_port=%d&mqtt_user=%63[^&]&mqtt_password=%63s",
               broker, &port, user, password) == 4) {
        ESP_LOGI("HTTP", "Received MQTT config: broker=%s, port=%d, user=%s", broker, port, user);

        // Zapisz dane MQTT do pamięci NVS
        save_mqtt_config_to_nvs(broker, port, user, password);

        httpd_resp_send(req, "MQTT configuration saved.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    } else {
        ESP_LOGE("HTTP", "Failed to parse MQTT configuration.");
        httpd_resp_send(req, "Invalid MQTT configuration format.", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }



}

void save_mqtt_config_to_nvs(const char* broker, int port, const char* user, const char* password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("mqtt_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Błąd przy otwieraniu NVS");
        return;
    }

    nvs_set_str(nvs_handle, "broker", broker);
    nvs_set_i32(nvs_handle, "port", port);
    nvs_set_str(nvs_handle, "user", user);
    nvs_set_str(nvs_handle, "password", password);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    ESP_LOGI("NVS", "Zapisano konfigurację MQTT: broker=%s, port=%d", broker, port);
}

void load_mqtt_config_from_nvs(char* broker, size_t broker_len, int* port, char* user, size_t user_len, char* password, size_t password_len) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("mqtt_config", NVS_READONLY, &nvs_handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW("MQTT", "Nie znaleziono konfiguracji MQTT w NVS. Używanie domyślnych wartości.");
        strncpy(broker, "mqtt://192.168.57.30", broker_len);
        *port = 1883;
        strncpy(user, "default_user", user_len);
        strncpy(password, "default_password", password_len);
        save_mqtt_config_to_nvs(broker, port, user, password); // Zapisz domyślne wartości
        return;
    } else if (err != ESP_OK) {
        ESP_LOGE("MQTT", "Błąd przy otwieraniu NVS: %s", esp_err_to_name(err));
        return;
    }

    if (nvs_get_str(nvs_handle, "broker", broker, &broker_len) != ESP_OK) {
        ESP_LOGW("MQTT", "Brak klucza 'broker' w NVS. Ustawianie domyślnej wartości.");
        strncpy(broker, "mqtt://192.168.57.30", broker_len);
    }
    if (nvs_get_i32(nvs_handle, "port", port) != ESP_OK) {
        ESP_LOGW("MQTT", "Brak klucza 'port' w NVS. Ustawianie domyślnej wartości.");
        *port = 1883;
    }
    if (nvs_get_str(nvs_handle, "user", user, &user_len) != ESP_OK) {
        ESP_LOGW("MQTT", "Brak klucza 'user' w NVS. Ustawianie domyślnej wartości.");
        strncpy(user, "default_user", user_len);
    }
    if (nvs_get_str(nvs_handle, "password", password, &password_len) != ESP_OK) {
        ESP_LOGW("MQTT", "Brak klucza 'password' w NVS. Ustawianie domyślnej wartości.");
        strncpy(password, "default_password", password_len);
    }

    nvs_close(nvs_handle);
    ESP_LOGI("MQTT", "Konfiguracja MQTT załadowana: broker=%s, port=%d, user=%s", broker, *port, user);
}
