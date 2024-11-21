#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt_client";

const char* broker_uri = "mqtt://192.168.0.66";

// Identyfikatory użytkownika, urządzenia i czujników
char user_id[20] = "user1";
char device_id[20] = "device1";
const char *photoresistor_id = "photoresistor";
const char *bmp280_id = "bmp280";

// Funkcja publikująca dane
void publish_data(esp_mqtt_client_handle_t client, const char *user, const char *device) {
    char light_topic[100];
    snprintf(light_topic, sizeof(light_topic), "/%s/%s/%s/light", user, device, photoresistor_id);

    char temperature_topic[100];
    char pressure_topic[100];
    snprintf(temperature_topic, sizeof(temperature_topic), "/%s/%s/%s/temperature", user, device, bmp280_id);
    snprintf(pressure_topic, sizeof(pressure_topic), "/%s/%s/%s/pressure", user, device, bmp280_id);

    const char *light_data = "{\"light\": 200}";
    const char *temperature_data = "{\"temperature\": 23.5}";
    const char *pressure_data = "{\"pressure\": 1013.25}";

    esp_mqtt_client_publish(client, light_topic, light_data, 0, 1, 0);
    ESP_LOGI(TAG, "Published light data on topic %s", light_topic);

    esp_mqtt_client_publish(client, temperature_topic, temperature_data, 0, 1, 0);
    ESP_LOGI(TAG, "Published temperature data on topic %s", temperature_topic);

    esp_mqtt_client_publish(client, pressure_topic, pressure_data, 0, 1, 0);
    ESP_LOGI(TAG, "Published pressure data on topic %s", pressure_topic);
}

// Task do publikacji danych co określony czas
void sensor_data_task(void *pvParameters) {
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t) pvParameters;
    while (1) {
        publish_data(client, user_id, device_id);
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}

// Funkcja obsługująca zdarzenia MQTT
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;

        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

// Funkcja inicjalizująca klienta MQTT i task publikujący dane
void mqtt_initialize(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
        .credentials.username = "username",
        .credentials.authentication.password = "password"
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    xTaskCreate(sensor_data_task, "sensor_data_task", 4096, (void*) client, 5, NULL);
}
