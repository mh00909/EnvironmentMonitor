#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "ota_update.h"

#define OTA_URL "https://drive.google.com/uc?export=download&id=1mfcf69EARp4S2OK0erOsyNZQVuEdKE9i"


void ota_task(void *pvParameter) {
    esp_http_client_config_t config = {
        .url = OTA_URL,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE("OTA", "Failed to initialize HTTP client");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE("OTA", "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    esp_ota_handle_t ota_handle;
    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    if (ota_partition == NULL) {
        ESP_LOGE("OTA", "Could not find OTA partition");
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t ota_begin = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (ota_begin != ESP_OK) {
        ESP_LOGE("OTA", "OTA begin failed: %s", esp_err_to_name(ota_begin));
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    char buffer[1024];
    int data_read;

    while ((data_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0) {
        esp_err_t ota_write = esp_ota_write(ota_handle, buffer, data_read);
        if (ota_write != ESP_OK) {
            ESP_LOGE("OTA", "Failed to write OTA data: %s", esp_err_to_name(ota_write));
            esp_ota_end(ota_handle);
            esp_http_client_cleanup(client);
            vTaskDelete(NULL);
            return;
        }
    }

    if (esp_ota_end(ota_handle) != ESP_OK) {
        ESP_LOGE("OTA", "OTA end failed");
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    if (esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
        ESP_LOGE("OTA", "Failed to set boot partition");
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_cleanup(client);
    ESP_LOGI("OTA", "OTA update successful, restarting...");
    esp_restart();
    
}
