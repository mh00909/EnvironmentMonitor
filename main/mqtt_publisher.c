#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "http_server.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "wifi_ap.h"
#include "wifi_station.h"
#include "esp_netif.h"
#include "bmp280.h"
#include "i2c_driver.h"
#include "mqtt_publisher.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "light_sensor.h"
#include "mqtt_client.h"
#include "ble_sensor.h"
#include "light_sensor.h"

static const char *TAG = "mqtt_client";

// Identyfikatory użytkownika, urządzenia i czujników
char user_id[20] = "user1";
char device_id[20] = "device1";
const char *photoresistor_id = "photoresistor";
const char *bmp280_id = "bmp280";

float min_temperature_threshold = 0.0;
float max_temperature_threshold = 10.0;
int min_light_threshold = 100;
int max_light_threshold = 800;

esp_mqtt_client_handle_t client_handle = NULL;
TaskHandle_t monitor_conditions_task_handle = NULL;

TaskHandle_t sensor_data_task_handle = NULL;
TaskHandle_t ble_data_task_handle = NULL;
SemaphoreHandle_t mqtt_mutex = NULL;

void initialize_mqtt_mutex() {
    mqtt_mutex = xSemaphoreCreateMutex();
    if (mqtt_mutex == NULL) {
        ESP_LOGE(TAG, "Błąd przy tworzeniu mutexa MQTT.");
    }
}
void safe_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data) {
    if (xSemaphoreTake(mqtt_mutex, portMAX_DELAY) == pdTRUE) {
        if (client != NULL) {
            esp_mqtt_client_publish(client, topic, data, 0, 1, 0);
        } 
        xSemaphoreGive(mqtt_mutex);
    } else {
        ESP_LOGE(TAG, "Błąd przy publikowaniu MQTT.");
    }
}

void mqtt_stop() {
    if (sensor_data_task_handle != NULL) {
        vTaskDelete(sensor_data_task_handle);
        sensor_data_task_handle = NULL;
    }
    if (ble_data_task_handle != NULL) {
        vTaskDelete(ble_data_task_handle);
        ble_data_task_handle = NULL;
    }

    if (client_handle != NULL) {
        if (mqtt_mutex != NULL) {
            xSemaphoreTake(mqtt_mutex, portMAX_DELAY); // Zabezpiecz przed użyciem w innych taskach
        }

        esp_err_t err = esp_mqtt_client_stop(client_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Pomyślnie zatrzymano klienta MQTT.");
        } else {
            ESP_LOGE(TAG, "Błąd przy tworzeniu klienta MQTT: %s", esp_err_to_name(err));
        }

        err = esp_mqtt_client_destroy(client_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Pomyślnie zniszczono klienta MQTT.");
        } else {
            ESP_LOGE(TAG, "Błąd przy niszczeniu klienta MQTT: %s", esp_err_to_name(err));
        }

        client_handle = NULL;

        if (mqtt_mutex != NULL) {
            xSemaphoreGive(mqtt_mutex); // Zwolnij mutex
        }
    } 
}


float generate_value(float min, float max) {
    return min + ((float)rand() / (float)RAND_MAX) * (max - min);
}

// Funkcja publikująca dane
// tematy: /<user_id>/<device_id>/<sensor_type>/<metric>
void publish_data(esp_mqtt_client_handle_t client, const char *user, const char *device) {

    //termometr
    if (current_temperature_ble != 0 && current_humidity_ble != 0) {
            ESP_LOGI("BLE", "Publishing BLE data: Temperature=%.2f°C, Humidity=%.2f%%",
                     current_temperature_ble, current_humidity_ble);

            char ble_temp_topic[100];
            char ble_humidity_topic[100];
            snprintf(ble_temp_topic, sizeof(ble_temp_topic), "/%s/%s/ble/temperature", user_id, device_id);
            snprintf(ble_humidity_topic, sizeof(ble_humidity_topic), "/%s/%s/ble/humidity", user_id, device_id);

            char ble_temp_data[50];
            char ble_humidity_data[50];
            snprintf(ble_temp_data, sizeof(ble_temp_data), "{\"temperature\": %.2f}",current_temperature_ble );
            snprintf(ble_humidity_data, sizeof(ble_humidity_data), "{\"humidity\": %.2f}", current_humidity_ble);

            safe_publish(client_handle, ble_temp_topic, ble_temp_data);
            safe_publish(client_handle, ble_humidity_topic, ble_humidity_data);
    }


    // czujniki

    bmp280_mode_t current_mode = bmp280_get_mode(); // Pobierz aktualny tryb pracy BMP280

    char light_topic[100];
    char temperature_topic[100];
    char pressure_topic[100];
    snprintf(temperature_topic, sizeof(temperature_topic), "/%s/%s/%s/temperature", user, device, bmp280_id);
    snprintf(pressure_topic, sizeof(pressure_topic), "/%s/%s/%s/pressure", user, device, bmp280_id);
    snprintf(light_topic, sizeof(light_topic), "/%s/%s/%s/light", user, device, photoresistor_id);

    light_sensor_read(&current_light);
    bmp280_read_data(&current_temperature_bmp280, &current_pressure_bmp280);
    current_pressure_bmp280 = current_pressure_bmp280/100;
    
    char light_data[50];
    char temperature_data[50];
    char pressure_data[50];
    snprintf(temperature_data, sizeof(temperature_data), "{\"temperature\": %.2f}", current_temperature_bmp280);
    snprintf(pressure_data, sizeof(pressure_data), "{\"pressure\": %.2f}", current_pressure_bmp280);
    snprintf(light_data, sizeof(light_data), "{\"light\": %d}", current_light);
    
    if (client_handle != NULL) {
        safe_publish(client, light_topic, light_data);
        safe_publish(client, temperature_topic, temperature_data);
        safe_publish(client, pressure_topic, pressure_data);

    }

}


void sensor_data_task(void *pvParameters) {
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t) pvParameters;

    while (1) {
        
        if (client_handle != NULL) {
            bmp280_mode_t current_mode = bmp280_get_mode();

            if (current_mode == BMP280_NORMAL_MODE) {
                ESP_LOGI(TAG, "BMP280 w trybie NORMAL_MODE. Publikacja danych.");
                publish_data(client, user_id, device_id);
            } else if (current_mode == BMP280_FORCED_MODE) {
                ESP_LOGI(TAG, "BMP280 w trybie FORCED_MODE. Wykonanie pojedynczego pomiaru i publikacja danych.");
                bmp280_set_mode(BMP280_FORCED_MODE); // Wymuszenie jednorazowego pomiaru
                while (bmp280_is_measuring()) {
                    vTaskDelay(pdMS_TO_TICKS(10)); // Poczekaj na zakończenie pomiaru
                }
                publish_data(client, user_id, device_id);
            } else {
              //  ESP_LOGI(TAG, "BMP280 w trybie SLEEP. Pomijanie publikacji danych.");
            }
        } 

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}


// Funkcja obsługująca zdarzenia MQTT
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    ESP_LOGI(TAG, "MQTT Event ID: %ld", event_id);

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            esp_mqtt_client_subscribe(client_handle, "/user1/device1/control/temp_range", 1);
            esp_mqtt_client_subscribe(client_handle, "/user1/device1/control/light_range", 1);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA received.");
            char topic[128] = {0};
            char data[128] = {0};
            snprintf(topic, event->topic_len + 1, "%.*s", event->topic_len, event->topic);
            snprintf(data, event->data_len + 1, "%.*s", event->data_len, event->data);

            ESP_LOGI(TAG, "Topic: %s", topic);
            ESP_LOGI(TAG, "Data: %s", data);

            if (strcmp(topic, "/user1/device1/control/temp_range") == 0) {
                float min_temp, max_temp;
                sscanf(data, "%f,%f", &min_temp, &max_temp);
                set_temperature_range(min_temp, max_temp);
                ESP_LOGI(TAG, "Updated temperature range: %.2f - %.2f", min_temp, max_temp);
            } else if (strcmp(topic, "/user1/device1/control/light_range") == 0) {
                int min_light, max_light;
                sscanf(data, "%d,%d", &min_light, &max_light);
                set_light_range(min_light, max_light);
                ESP_LOGI(TAG, "Updated light range: %d - %d", min_light, max_light);
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

void set_temperature_range(float min_temp, float max_temp) {
    min_temperature_threshold = min_temp;
    max_temperature_threshold = max_temp;
}

void set_light_range(int min_light, int max_light) {
    min_light_threshold = min_light;
    max_light_threshold = max_light;
}

void mqtt_initialize(void) {
    char broker[128] = {0};
    int port = 1883; // Domyślny port
    char user[64] = {0};
    char password[64] = {0};

    load_mqtt_config_from_nvs(broker, sizeof(broker), &port, user, sizeof(user), password, sizeof(password));

    if (strlen(broker) == 0) {
        ESP_LOGE(TAG, "Broker MQTT nie skonfigurowany.");
        return;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker,
        .broker.address.port = port,
        .credentials.username = user[0] ? user : NULL,
        .credentials.authentication.password = password[0] ? password : NULL,
    };

    client_handle = esp_mqtt_client_init(&mqtt_cfg);
    if (client_handle == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client.");
        return;
    }

    esp_mqtt_client_register_event(client_handle, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    

    esp_err_t err = esp_mqtt_client_start(client_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT client started successfully.");
        xTaskCreate(sensor_data_task, "sensor_data_task", 4096, (void*) client_handle, 5, &sensor_data_task_handle);
      
    } else {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        client_handle = NULL;
    }
}
