#include "power_manager.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "nvs.h"
#include "mqtt_publisher.h"
#include "wifi_station.h"
#include "wifi_ap.h"
#include "http_server.h"


config_esp32 esp32_config = {
    .power_mode = 0, // Domyślny tryb: normalny
    .deep_sleep_duration = 60 // Domyślny czas deep sleep
};
void power_manager_task(void* pvParameters) {
    static int last_power_mode = -1;

    while (1) {
        switch (esp32_config.power_mode) {
            case 0: // Tryb normalny
                if (last_power_mode != 0) {
                    ESP_LOGI("POWER_MANAGER", "Tryb normalny: Wszystkie funkcje aktywne.");
                    esp_wifi_start();
                }
                last_power_mode = 0;
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;

            case 1: // Tryb oszczędzania energii
                // Kod oszczędzania energii (bez zmian)
                last_power_mode = 1;
                break;

            case 2: // Tryb deep sleep
                if (last_power_mode != 2) {
                    ESP_LOGI("POWER_MANAGER", "Uruchamianie Wi-Fi AP i serwera HTTP na 30 sekund...");

                    // Inicjalizacja Wi-Fi w trybie AP
                    esp_wifi_stop();
                    vTaskDelay(pdMS_TO_TICKS(500)); // Krótkie opóźnienie
                    wifi_init_ap();

                    // Uruchamianie serwera HTTP
                    config_completed = false; // Resetuj flagę
                    start_webserver();

                    // Oczekiwanie na zakończenie konfiguracji lub timeout
                    uint32_t elapsed_time = 0;
                    while (elapsed_time < 30000 && !config_completed) {
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        elapsed_time += 1000;
                    }

                    // Zatrzymanie serwera HTTP i Wi-Fi AP
                    if (server != NULL) stop_webserver(&server);
                    esp_wifi_stop();
                    ESP_LOGI("POWER_MANAGER", "Wi-Fi AP i serwer HTTP zatrzymane.");

                    // Przejście w tryb deep sleep
                    ESP_LOGI("POWER_MANAGER", "Deep sleep na %d sekund.", esp32_config.deep_sleep_duration);
                    esp_sleep_enable_ext1_wakeup((1ULL << GPIO_NUM_13), ESP_EXT1_WAKEUP_ALL_LOW);
                    esp_sleep_enable_timer_wakeup(esp32_config.deep_sleep_duration * 1000000);
                    esp_deep_sleep_start();
                }
                last_power_mode = 2;
                break;

            default:
                ESP_LOGW("POWER_MANAGER", "Nieznany tryb pracy.");
                vTaskDelay(pdMS_TO_TICKS(1000));
                last_power_mode = -1;
                break;
        }
    }
}


void save_device_config_to_nvs() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("config", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_i32(nvs_handle, "power_mode", esp32_config.power_mode);
        nvs_set_i32(nvs_handle, "deep_sleep_dur", esp32_config.deep_sleep_duration);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI("NVS", "Konfiguracja zapisana w NVS.");
    } else {
        ESP_LOGE("NVS", "Błąd podczas zapisu konfiguracji: %s", esp_err_to_name(err));
    }
}

int load_device_config_from_nvs() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("config", NVS_READONLY, &nvs_handle);

    if (err == ESP_OK) {
        // Odczyt power_mode
        if (nvs_get_i32(nvs_handle, "power_mode", &esp32_config.power_mode) != ESP_OK) {
            esp32_config.power_mode = 0; // Domyślny tryb: Normalny
        }

        // Odczyt deep_sleep_duration
        if (nvs_get_i32(nvs_handle, "deep_sleep_dur", &esp32_config.deep_sleep_duration) != ESP_OK) {
            esp32_config.deep_sleep_duration = 60; // Domyślny czas deep sleep (60 sekund)
        }

        nvs_close(nvs_handle);
        ESP_LOGI("NVS", "Załadowano konfigurację z NVS: power_mode=%d, deep_sleep_duration=%d",
                 esp32_config.power_mode, esp32_config.deep_sleep_duration);
    } else {
        ESP_LOGW("NVS", "Brak zapisanej konfiguracji w NVS. Użycie domyślnych wartości.");
        esp32_config.power_mode = 0;             // Domyślny tryb: Normalny
        esp32_config.deep_sleep_duration = 60;   // Domyślny czas deep sleep
    }

    return esp32_config.power_mode;
}
