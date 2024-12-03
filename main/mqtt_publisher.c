#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "wifi_ap.h"
#include "wifi_station.h"
#include "esp_netif.h"
#include "mqtt_publisher.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt_client";

const char* broker_uri = "mqtt://192.168.128.30";

// Identyfikatory użytkownika, urządzenia i czujników
char user_id[20] = "user1";
char device_id[20] = "device1";
const char *photoresistor_id = "photoresistor";
const char *bmp280_id = "bmp280";


static esp_mqtt_client_handle_t client_handle = NULL;

TaskHandle_t sensor_data_task_handle = NULL;
SemaphoreHandle_t mqtt_mutex = NULL;

void initialize_mqtt_mutex() {
    mqtt_mutex = xSemaphoreCreateMutex();
    if (mqtt_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT mutex.");
    }
}
void safe_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data) {
    if (xSemaphoreTake(mqtt_mutex, portMAX_DELAY) == pdTRUE) {
        if (client != NULL) {
            esp_mqtt_client_publish(client, topic, data, 0, 1, 0);
        } else {
            ESP_LOGW(TAG, "MQTT client handle is NULL. Skipping publish.");
        }
        xSemaphoreGive(mqtt_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take MQTT mutex.");
    }
}

void mqtt_stop() {
    if (sensor_data_task_handle != NULL) {
        vTaskDelete(sensor_data_task_handle);
        sensor_data_task_handle = NULL;
    }

    if (client_handle != NULL) {
        if (mqtt_mutex != NULL) {
            xSemaphoreTake(mqtt_mutex, portMAX_DELAY); // Zabezpiecz przed użyciem w innych taskach
        }

        esp_err_t err = esp_mqtt_client_stop(client_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "MQTT client stopped successfully.");
        } else {
            ESP_LOGE(TAG, "Failed to stop MQTT client. Error: %s", esp_err_to_name(err));
        }

        err = esp_mqtt_client_destroy(client_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "MQTT client destroyed successfully.");
        } else {
            ESP_LOGE(TAG, "Failed to destroy MQTT client. Error: %s", esp_err_to_name(err));
        }

        client_handle = NULL;

        if (mqtt_mutex != NULL) {
            xSemaphoreGive(mqtt_mutex); // Zwolnij mutex
        }
    } else {
        ESP_LOGW(TAG, "MQTT client is not running or already stopped.");
    }
}


float generate_value(float min, float max) {
    return min + ((float)rand() / (float)RAND_MAX) * (max - min);
}

// Funkcja publikująca dane
// tematy: /<user_id>/<device_id>/<sensor_type>/<metric>
void publish_data(esp_mqtt_client_handle_t client, const char *user, const char *device) {

    char light_topic[100];
    char temperature_topic[100];
    char pressure_topic[100];
    snprintf(temperature_topic, sizeof(temperature_topic), "/%s/%s/%s/temperature", user, device, bmp280_id);
    snprintf(pressure_topic, sizeof(pressure_topic), "/%s/%s/%s/pressure", user, device, bmp280_id);
    snprintf(light_topic, sizeof(light_topic), "/%s/%s/%s/light", user, device, photoresistor_id);

    // Generowanie losowych wartości
    float light = generate_value(100.0, 800.0);
    float temperature = generate_value(20.0, 30.0); 
    float pressure = generate_value(1000.0, 1025.0); 

    // Tworzenie danych w formacie JSON
    char light_data[50];
    char temperature_data[50];
    char pressure_data[50];
    snprintf(temperature_data, sizeof(temperature_data), "{\"temperature\": %.2f}", temperature);
    snprintf(light_data, sizeof(light_data), "{\"light\": %.2f}", light);
    snprintf(pressure_data, sizeof(pressure_data), "{\"pressure\": %.2f}", pressure);
    
    if (client_handle != NULL) {
        safe_publish(client, light_topic, light_data);
        ESP_LOGI(TAG, "Published light data on topic %s", light_topic);

        safe_publish(client, temperature_topic, temperature_data);
        ESP_LOGI(TAG, "Published temperature data on topic %s", temperature_topic);

        safe_publish(client, pressure_topic, pressure_data);
        ESP_LOGI(TAG, "Published pressure data on topic %s", pressure_topic);

    }
}

// Task do publikacji danych co określony czas
void sensor_data_task(void *pvParameters) {
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t) pvParameters;
    while (1) {
        if (client_handle != NULL) {
            publish_data(client, user_id, device_id);
        } else {
            ESP_LOGW(TAG, "MQTT client is not initialized. Skipping publish.");
        }
        vTaskDelay(10000 / portTICK_PERIOD_MS); // 10 s
    }
}

// Funkcja obsługująca zdarzenia MQTT
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;

    char topic[128];
    char payload[128];

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            esp_mqtt_client_subscribe(client_handle, "/user1/device1/config", 0);
            ESP_LOGI(TAG, "Subscribed to /user1/device1/config");
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

            if (event->topic_len < sizeof(topic) && event->data_len < sizeof(payload)) {
                memcpy(topic, event->topic, event->topic_len);
                memcpy(payload, event->data, event->data_len);
                topic[event->topic_len] = '\0';
                payload[event->data_len] = '\0';

                if (strcmp(topic, "/user1/device1/config") == 0) {
                    if (strcmp(payload, "switch_to_ap") == 0) {
                        ESP_LOGI(TAG, "Received command: switch_to_ap");
                        xTaskNotify(config_task_handle, 1, eSetValueWithoutOverwrite);
                    } else if (strcmp(payload, "switch_to_sta") == 0) {
                        ESP_LOGI(TAG, "Received command: switch_to_sta");
                        xTaskNotify(config_task_handle, 2, eSetValueWithoutOverwrite); // Przełącz na tryb STA
                    } else {
                        ESP_LOGW(TAG, "Unknown command: %s", payload);
                    }
                }
            } else {
                ESP_LOGW(TAG, "Received topic or payload is too long");
            }
            
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
    if (client_handle != NULL) {
        ESP_LOGW(TAG, "MQTT client is already initialized. Stopping current client.");
        mqtt_stop();
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
        .credentials.username = "username",
        .credentials.authentication.password = "password"
    };

    client_handle = esp_mqtt_client_init(&mqtt_cfg);
    if (client_handle == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client.");
        return;
    }

    esp_err_t err = esp_mqtt_client_register_event(client_handle, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(err));
        return;
    }

    err = esp_mqtt_client_start(client_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT client started successfully.");
        xTaskCreate(sensor_data_task, "sensor_data_task", 4096, (void*) client_handle, 5, &sensor_data_task_handle);
    } else {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        client_handle = NULL;
    }
}
