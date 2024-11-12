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
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_mac.h"

static const char *TAG = "mqtt_client";

// Identyfikatory użytkownika, urządzenia i czujników
const char *user_id = "user1";
const char *device_id = "device1";
const char *photoresistor_id = "photoresistor";
const char *bmp280_id = "bmp280";



void publish_data(esp_mqtt_client_handle_t client) {
    // Tematy dla fotorezystora
    char light_topic[100];
    snprintf(light_topic, sizeof(light_topic), "/%s/%s/%s/light", user_id, device_id, photoresistor_id);

    // Tematy dla BMP280
    char temperature_topic[100];
    char pressure_topic[100];
    snprintf(temperature_topic, sizeof(temperature_topic), "/%s/%s/%s/temperature", user_id, device_id, bmp280_id);
    snprintf(pressure_topic, sizeof(pressure_topic), "/%s/%s/%s/pressure", user_id, device_id, bmp280_id);

    // Symulowane dane czujników
    const char *light_data = "{\"light\": 200}";         // Przykładowa wartość natężenia światła
    const char *temperature_data = "{\"temperature\": 23.5}";  // Przykładowa wartość temperatury
    const char *pressure_data = "{\"pressure\": 1013.25}";    // Przykładowa wartość ciśnienia

    // Publikacja danych dla fotorezystora
    esp_mqtt_client_publish(client, light_topic, light_data, 0, 1, 0);
    ESP_LOGI("mqtt_client", "Published light data on topic %s", light_topic);

    // Publikacja danych dla BMP280
    esp_mqtt_client_publish(client, temperature_topic, temperature_data, 0, 1, 0);
    ESP_LOGI("mqtt_client", "Published temperature data on topic %s", temperature_topic);
    
    esp_mqtt_client_publish(client, pressure_topic, pressure_data, 0, 1, 0);
    ESP_LOGI("mqtt_client", "Published pressure data on topic %s", pressure_topic);
}


// Funkcja obsługująca zdarzenia MQTT
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            publish_data(client);  // Publikacja danych po połączeniu
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
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;

        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

// Funkcja inicjalizująca klienta MQTT
void mqtt_initialize(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://192.168.128.30",  // Adres IP brokera
        .credentials.username = "username",             // Nazwa użytkownika
        .credentials.authentication.password = "password" // Hasło
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}
