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
#include "../../v5.3.1/esp-idf/components/json/cJSON/cJSON.h"


static const char *TAG = "mqtt_client";

user_t users[5];    // Maksymalnie 5 użytkowników
int user_count = 0; 

float min_temperature_threshold = 0.0;
float max_temperature_threshold = 40.0;
int min_light_threshold = 0;
int max_light_threshold = 900;

int client_count = 0;

bool mqtt_connected = false;
bool mqtt_initialized = false;


esp_mqtt_client_handle_t client_handle = NULL;
TaskHandle_t monitor_conditions_task_handle = NULL;
int topic_handler_count = 0;
TaskHandle_t sensor_data_task_handle = NULL;
TaskHandle_t ble_data_task_handle = NULL;
SemaphoreHandle_t mqtt_mutex = NULL;


static SemaphoreHandle_t clients_mutex = NULL;

void initialize_global_mutexes() {
    clients_mutex = xSemaphoreCreateMutex();
    if (clients_mutex == NULL) {
        ESP_LOGE(TAG, "Nie udało się utworzyć globalnego mutexa.");
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    ESP_LOGI("MQTT_EVENT", "MQTT Event ID: %ld", event_id);

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI("MQTT_EVENT", "Połączono z brokerem MQTT.");


            esp_mqtt_client_subscribe(client_handle, "/system/add_client", 1);
            esp_mqtt_client_subscribe(client_handle, "/system/add_device", 1);
            esp_mqtt_client_subscribe(client_handle, "/system/add_metric", 1);
            esp_mqtt_client_subscribe(client_handle, "/system/add_sensor", 1);
            
            esp_mqtt_client_subscribe(client_handle, "/system/settings/temp_range", 1);
            esp_mqtt_client_subscribe(client_handle, "/system/settings/light_range", 1);
            mqtt_connected = true;

            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI("MQTT_EVENT", "Rozłączono z brokerem MQTT.");
            mqtt_connected = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI("MQTT_EVENT", "Subskrypcja zakończona sukcesem, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI("MQTT_EVENT", "Odsubskrybowano, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI("MQTT_EVENT", "Wiadomość opublikowana, msg_id=%d", event->msg_id);
            break;

       case MQTT_EVENT_DATA:
            ESP_LOGI("MQTT_EVENT", "Odebrano wiadomość.");
            ESP_LOGI("MQTT_EVENT", "Temat: %.*s", event->topic_len, event->topic);
            ESP_LOGI("MQTT_EVENT", "Dane: %.*s", event->data_len, event->data);

            char topic[128];
            snprintf(topic, sizeof(topic), "%.*s", event->topic_len, event->topic);
            char data[256];
            snprintf(data, sizeof(data), "%.*s", event->data_len, event->data);


            if (strncmp(event->topic, "/system/settings/temp_range", event->topic_len) == 0) {
                float min_temp, max_temp;
                sscanf(event->data, "{\"min_temperature\":%f,\"max_temperature\":%f}", &min_temp, &max_temp);
                
                min_temperature_threshold = min_temp;
                max_temperature_threshold = max_temp;
                printf("Updated temperature range: Min=%.2f, Max=%.2f\n", min_temperature_threshold, max_temperature_threshold);
            } else if (strncmp(event->topic, "/system/settings/light_range", event->topic_len) == 0) {
                int min_light, max_light;
                sscanf(event->data, "{\"min_light\":%d,\"max_light\":%d}", &min_light, &max_light);
                min_light_threshold = min_light;
                max_light_threshold = max_light;
                printf("Updated light range: Min=%d, Max=%d\n", min_light_threshold, max_light_threshold);
            }


            else if (strcmp(topic, "/system/add_client") == 0) {
                ESP_LOGI(TAG, "Dodawanie klienta: %s", data);
                handle_add_user(data);
            } else if (strcmp(topic, "/system/add_device") == 0) {
                ESP_LOGI(TAG, "Dodawanie urządzenia: %s", data);
                handle_add_device(data);
            } else if (strcmp(topic, "/system/add_sensor") == 0) {
                ESP_LOGI(TAG, "Dodawanie czujnika: %s", data);
                handle_add_sensor(data);
            } else if (strcmp(topic, "/system/add_metric") == 0) {
                ESP_LOGI(TAG, "Dodawanie metryki: %s", data);
                handle_add_metric(data);
            } 
            
            else {
                ESP_LOGW(TAG, "Nieobsługiwany temat: %s", topic);
            }
            break;


        case MQTT_EVENT_ERROR:
            ESP_LOGE("MQTT_EVENT", "Wystąpił błąd MQTT.");
            break;

        default:
            ESP_LOGI("MQTT_EVENT", "Inne zdarzenie: %ld", event_id);
            break;
    }
}


void publish_data_for_all_clients() {
    ESP_LOGI("MQTT", "Rozpoczynam publikację danych dla wszystkich klientów...");

    for (int i = 0; i < user_count; i++) {
        for (int j = 0; j < users[i].device_count; j++) {
            for (int k = 0; k < users[i].devices[j].sensor_count; k++) {
                for (int m = 0; m < users[i].devices[j].sensors[k].metric_count; m++) {
                    char topic[128];
                    generate_mqtt_topic(
                        topic, sizeof(topic),
                        users[i].user_id,
                        users[i].devices[j].device_id,
                        users[i].devices[j].sensors[k].sensor_type,
                        users[i].devices[j].sensors[k].metrics[m].metric
                    );

                    char value[128];
                    if (strcmp(users[i].devices[j].sensors[k].sensor_type, "bmp280") == 0) {
                        // Dane z BMP280
                        float temperature, pressure;
                        bmp280_read_data(&temperature, &pressure);
                        pressure /= 100.0; // Konwersja do hPa

                        if (strcmp(users[i].devices[j].sensors[k].metrics[m].metric, "temperature") == 0) {
                            snprintf(value, sizeof(value), "{\"temperature\": %.2f}", temperature);
                        } else if (strcmp(users[i].devices[j].sensors[k].metrics[m].metric, "pressure") == 0) {
                            snprintf(value, sizeof(value), "{\"pressure\": %.2f}", pressure);
                        }
                    } else if (strcmp(users[i].devices[j].sensors[k].sensor_type, "photoresistor") == 0) {
                        // Dane z fotorezystora
                        int light_level = 0;
                        light_sensor_read(&light_level);
                        snprintf(value, sizeof(value), "{\"light\": %d}", light_level);
                    } else if (strcmp(users[i].devices[j].sensors[k].sensor_type, "ble") == 0) {
                        // Dane z BLE
                        if (strcmp(users[i].devices[j].sensors[k].metrics[m].metric, "temperature") == 0) {
                            snprintf(value, sizeof(value), "{\"temperature\": %.2f}", current_temperature_ble);
                        } else if (strcmp(users[i].devices[j].sensors[k].metrics[m].metric, "humidity") == 0) {
                            snprintf(value, sizeof(value), "{\"humidity\": %.2f}", current_humidity_ble);
                        }
                    }

                    safe_publish(client_handle, topic, value);
                }
            }
        }
    }

    ESP_LOGI(TAG, "Publikacja danych zakończona.");
}



void initialize_mqtt_mutex() {
    mqtt_mutex = xSemaphoreCreateMutex();
    if (mqtt_mutex == NULL) {
        ESP_LOGE(TAG, "Błąd przy tworzeniu mutexa MQTT.");
    }
}

void safe_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data) {
    if (!client || !topic || !data) {
        ESP_LOGE("MQTT", "Nieprawidłowe parametry w safe_publish.");
        return;
    }

    if (!mqtt_connected) {
        ESP_LOGE("MQTT", "MQTT nie jest połączony.");
        return;
    }

    if (mqtt_mutex == NULL) {
        ESP_LOGE("MQTT", "Mutex MQTT nie został poprawnie zainicjalizowany.");
        return;
    }

    // Zablokuj mutex
    if (xSemaphoreTake(mqtt_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        esp_err_t err = esp_mqtt_client_publish(client, topic, data, 0, 1, 0);
        if (err == ESP_OK) {
            ESP_LOGI("MQTT", "Pomyślnie opublikowano dane na temat: %s", topic);
        } else {
            //ESP_LOGE("MQTT", "Błąd publikacji na temat %s: %s", topic, esp_err_to_name(err));
        }
        xSemaphoreGive(mqtt_mutex);
    } else {
        ESP_LOGE("MQTT", "Nie udało się zablokować mutexa MQTT.");
    }
}



void mqtt_stop() {
    if (mqtt_connected) {
        esp_mqtt_client_disconnect(client_handle);
    }


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

void publish_bmp280_data(const char *user, const char *device) {
    float temperature, pressure;
    bmp280_read_data(&temperature, &pressure);
    pressure /= 100.0;

    char temperature_topic[100], pressure_topic[100];
    snprintf(temperature_topic, sizeof(temperature_topic), "/%s/%s/bmp280/temperature", user, device);
    snprintf(pressure_topic, sizeof(pressure_topic), "/%s/%s/bmp280/pressure", user, device);

    char temperature_data[50], pressure_data[50];
    snprintf(temperature_data, sizeof(temperature_data), "{\"temperature\": %.2f}", temperature);
    snprintf(pressure_data, sizeof(pressure_data), "{\"pressure\": %.2f}", pressure);

    safe_publish(client_handle, temperature_topic, temperature_data);
    safe_publish(client_handle, pressure_topic, pressure_data);
}
void publish_light_sensor_data(const char *user, const char *device) {
    int light_level;
    light_sensor_read(&light_level);

    char light_topic[100];
    snprintf(light_topic, sizeof(light_topic), "/%s/%s/photoresistor/light", user, device);

    char light_data[50];
    snprintf(light_data, sizeof(light_data), "{\"light\": %d}", light_level);

    safe_publish(client_handle, light_topic, light_data);
}

void publish_ble_data(const char *user, const char *device) {
    if (ble_connected) {
        esp_err_t ble_status = read_ble_data();
        if (ble_status == ESP_OK) {
            char temperature_topic[100], humidity_topic[100];
            snprintf(temperature_topic, sizeof(temperature_topic), "/%s/%s/ble/temperature", user, device);
            snprintf(humidity_topic, sizeof(humidity_topic), "/%s/%s/ble/humidity", user, device);

            char temperature_data[50], humidity_data[50];
            snprintf(temperature_data, sizeof(temperature_data), "{\"temperature\": %.2f}", current_temperature_ble);
            snprintf(humidity_data, sizeof(humidity_data), "{\"humidity\": %.2f}", current_humidity_ble);

            safe_publish(client_handle, temperature_topic, temperature_data);
            safe_publish(client_handle, humidity_topic, humidity_data);
        }
    }
}


void sensor_data_task(void *pvParameters) {
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t) pvParameters;

    bmp280_config_t config;
    load_bmp280_config_from_nvs(&config); // Wczytaj tryb BMP280 z konfiguracji

    while (1) {
        if (config.mode == BMP280_NORMAL_MODE) {
            // Automatyczna publikacja danych w trybie NORMAL
            for (int i = 0; i < user_count; i++) {
                for (int j = 0; j < users[i].device_count; j++) {
                    publish_bmp280_data(users[i].user_id, users[i].devices[j].device_id);
                    publish_light_sensor_data(users[i].user_id, users[i].devices[j].device_id);
                    publish_ble_data(users[i].user_id, users[i].devices[j].device_id);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(30000)); // 30 sekund
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

void restart_mqtt_client() {
    if (client_handle) {
        mqtt_stop(); // Zatrzymaj istniejącego klienta
    }
    mqtt_initialize(); // Ponownie zainicjalizuj klienta MQTT
}

void handle_temp_range(const char *topic, const char *data) {
    float min_temp, max_temp;
    sscanf(data, "%f,%f", &min_temp, &max_temp);
    set_temperature_range(min_temp, max_temp);
    ESP_LOGI(TAG, "Updated temperature range: %.2f - %.2f", min_temp, max_temp);
}

void handle_light_range(const char *topic, const char *data) {
    int min_light, max_light;
    sscanf(data, "%d,%d", &min_light, &max_light);
    set_light_range(min_light, max_light);
    ESP_LOGI(TAG, "Updated light range: %d - %d", min_light, max_light);
}




void mqtt_initialize(void) {
    if (mqtt_initialized) {
        ESP_LOGW(TAG, "MQTT już zainicjalizowany.");
        return;
    }
    initialize_global_mutexes();
    initialize_mqtt_mutex();
    if (mqtt_mutex == NULL) {
        ESP_LOGE(TAG, "Nie udało się utworzyć mutexa MQTT. Przerywanie inicjalizacji.");
        return;
    }

    char broker[128] = "";
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
        .credentials.username = user,
    .credentials.authentication.password = password,
        .network.timeout_ms = 20000,
        .session.keepalive = 240, 
    };

    ESP_LOGI(TAG, "MQTT broker: %s", mqtt_cfg.broker.address.uri);
    ESP_LOGI(TAG, "MQTT port: %ld", mqtt_cfg.broker.address.port);
    ESP_LOGI(TAG, "MQTT username: %s", mqtt_cfg.credentials.username ? mqtt_cfg.credentials.username : "NULL");
    ESP_LOGI(TAG, "MQTT password: %s", mqtt_cfg.credentials.authentication.password ? "SET" : "NULL");



    client_handle = esp_mqtt_client_init(&mqtt_cfg);
    if (client_handle == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client.");
        return;
    }

    esp_mqtt_client_register_event(client_handle, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_err_t err = esp_mqtt_client_start(client_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT client started successfully.");
        xTaskCreate(sensor_data_task, "sensor_data_task", 10240, (void*) client_handle, 5, &sensor_data_task_handle);
      
    } else {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        client_handle = NULL;
    }



    mqtt_initialized = true;


    // Subskrypcja na temat
    esp_mqtt_client_subscribe(client_handle, "/system/add_client", 1);
    esp_mqtt_client_subscribe(client_handle, "/system/add_device", 1);
    esp_mqtt_client_subscribe(client_handle, "/system/add_metric", 1);
    esp_mqtt_client_subscribe(client_handle, "/system/add_sensor", 1);
    
    esp_mqtt_client_subscribe(client_handle, "/system/settings/temp_range", 1);
    esp_mqtt_client_subscribe(client_handle, "/system/settings/light_range", 1);
}


int add_user(const char *user_id) {
    if (user_count >= MAX_USERS) {
        ESP_LOGE(TAG, "Maksymalna liczba użytkowników osiągnięta.");
        return -1;
    }
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].user_id, user_id) == 0) {
            ESP_LOGI(TAG, "Użytkownik już istnieje: %s", user_id);
            return 0;
        }
    }
    strncpy(users[user_count].user_id, user_id, sizeof(users[user_count].user_id) - 1);
    users[user_count].device_count = 0;
    user_count++;
    ESP_LOGI(TAG, "Dodano użytkownika: %s", user_id);
    return 0;
}



int add_device(const char *user_id, const char *device_id) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].user_id, user_id) == 0) {
            for (int j = 0; j < users[i].device_count; j++) {
                if (strcmp(users[i].devices[j].device_id, device_id) == 0) {
                    ESP_LOGI(TAG, "Urządzenie już istnieje: %s", device_id);
                    return 0;
                }
            }
            if (users[i].device_count >= MAX_DEVICES) {
                ESP_LOGE(TAG, "Maksymalna liczba urządzeń osiągnięta.");
                return -1;
            }
            strncpy(users[i].devices[users[i].device_count].device_id, device_id, sizeof(users[i].devices[users[i].device_count].device_id) - 1);
            users[i].devices[users[i].device_count].sensor_count = 0;
            users[i].device_count++;
            ESP_LOGI(TAG, "Dodano urządzenie: %s do użytkownika: %s", device_id, user_id);
            return 0;
        }
    }
    ESP_LOGE(TAG, "Użytkownik nie znaleziony: %s", user_id);
    return -1;
}


int add_sensor(const char *user_id, const char *device_id, const char *sensor_type) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].user_id, user_id) == 0) {
            for (int j = 0; j < users[i].device_count; j++) {
                if (strcmp(users[i].devices[j].device_id, device_id) == 0) {
                    if (users[i].devices[j].sensor_count >= MAX_SENSORS) {
                        ESP_LOGE("MQTT", "Maksymalna liczba sensorów osiągnięta.");
                        return -1;
                    }
                    strncpy(users[i].devices[j].sensors[users[i].devices[j].sensor_count].sensor_type, sensor_type, sizeof(users[i].devices[j].sensors[users[i].devices[j].sensor_count].sensor_type) - 1);
                    users[i].devices[j].sensors[users[i].devices[j].sensor_count].metric_count = 0;
                    users[i].devices[j].sensor_count++;
                    ESP_LOGI("MQTT", "Dodano sensor: %s do urządzenia: %s", sensor_type, device_id);
                    return 0;
                }
            }
        }
    }
    ESP_LOGE("MQTT", "Urządzenie lub użytkownik nie znalezione.");
    return -1;
}

int add_metric(const char *user_id, const char *device_id, const char *sensor_type, const char *metric) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].user_id, user_id) == 0) {
            for (int j = 0; j < users[i].device_count; j++) {
                if (strcmp(users[i].devices[j].device_id, device_id) == 0) {
                    for (int k = 0; k < users[i].devices[j].sensor_count; k++) {
                        if (strcmp(users[i].devices[j].sensors[k].sensor_type, sensor_type) == 0) {
                            if (users[i].devices[j].sensors[k].metric_count >= MAX_METRICS) {
                                ESP_LOGE("MQTT", "Maksymalna liczba metryk osiągnięta.");
                                return -1;
                            }
                            strncpy(users[i].devices[j].sensors[k].metrics[users[i].devices[j].sensors[k].metric_count].metric, metric, sizeof(users[i].devices[j].sensors[k].metrics[users[i].devices[j].sensors[k].metric_count].metric) - 1);
                            users[i].devices[j].sensors[k].metric_count++;
                            ESP_LOGI("MQTT", "Dodano metrykę: %s do sensora: %s", metric, sensor_type);
                            return 0;
                        }
                    }
                }
            }
        }
    }
    ESP_LOGE("MQTT", "Sensor, urządzenie lub użytkownik nie znalezione.");
    return -1;
}



void publish_all_metrics(const char *user_id, const char *device_id) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].user_id, user_id) == 0) {
            for (int j = 0; j < users[i].device_count; j++) {
                if (strcmp(users[i].devices[j].device_id, device_id) == 0) {
                    for (int k = 0; k < users[i].devices[j].sensor_count; k++) {
                        for (int m = 0; m < users[i].devices[j].sensors[k].metric_count; m++) {
                            char topic[128];
                            generate_mqtt_topic(topic, sizeof(topic),
                                                user_id, device_id,
                                                users[i].devices[j].sensors[k].sensor_type,
                                                users[i].devices[j].sensors[k].metrics[m].metric);

                            char value[50];
                            snprintf(value, sizeof(value), "{\"value\": %d}", 20);
                            safe_publish(client_handle, topic, value);
                        }
                    }
                }
            }
        }
    }
}

void subscribe_all_topics(const char *user_id) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].user_id, user_id) == 0) {
            for (int j = 0; j < users[i].device_count; j++) {
                for (int k = 0; k < users[i].devices[j].sensor_count; k++) {
                    for (int m = 0; m < users[i].devices[j].sensors[k].metric_count; m++) {
                        char topic[128];
                        generate_mqtt_topic(topic, sizeof(topic),
                                            user_id, users[i].devices[j].device_id,
                                            users[i].devices[j].sensors[k].sensor_type,
                                            users[i].devices[j].sensors[k].metrics[m].metric);

                        esp_mqtt_client_subscribe(client_handle, topic, 1);
                        ESP_LOGI("MQTT", "Subskrybowano temat: %s", topic);
                    }
                }
            }
        }
    }
}
void generate_mqtt_topic(char *topic, size_t topic_size, const char *user_id, const char *device_id, const char *sensor_type, const char *metric) {
    if (!topic || topic_size == 0 || !user_id || !device_id || !sensor_type || !metric) {
        ESP_LOGE("MQTT", "Nieprawidłowe argumenty dla generate_mqtt_topic");
        return;
    }

    // Formatuj temat MQTT
    
    snprintf(topic, topic_size, "user/%s/%s/%s/%s", user_id, device_id, sensor_type, metric);
    ESP_LOGI(TAG, "Generowany temat MQTT: %s", topic);

}

void subscribe_all_users() {
    for (int i = 0; i < user_count; i++) {
        subscribe_all_topics(users[i].user_id);
    }
}




void handle_add_user(const char *data) {
    ESP_LOGI(TAG, "Przetwarzanie add_user: %s", data);
    cJSON *root = cJSON_Parse(data);
    if (!root) {
        ESP_LOGE(TAG, "Błąd parsowania JSON dla add_user: %s", data);
        return;
    }

    const cJSON *user_id = cJSON_GetObjectItem(root, "user_id");
    if (!cJSON_IsString(user_id)) {
        ESP_LOGE(TAG, "Nieprawidłowy format JSON dla add_user: brak user_id");
        cJSON_Delete(root);
        return;
    }

    if (add_user(user_id->valuestring) == 0) {
        ESP_LOGI(TAG, "Dodano użytkownika: %s", user_id->valuestring);
    } else {
        ESP_LOGE(TAG, "Nie udało się dodać użytkownika: %s", user_id->valuestring);
    }

    cJSON_Delete(root);
}



void handle_add_device(const char *data) {
    ESP_LOGI(TAG, "Przetwarzanie add_device: %s", data);

    cJSON *root = cJSON_Parse(data);
    if (!root) {
        ESP_LOGE(TAG, "Błąd parsowania JSON dla add_device: %s", data);
        return;
    }

    const cJSON *user_id = cJSON_GetObjectItem(root, "user_id");
    const cJSON *device_id = cJSON_GetObjectItem(root, "device_id");

    if (!cJSON_IsString(user_id) || !cJSON_IsString(device_id)) {
        ESP_LOGE(TAG, "Nieprawidłowy format JSON w add_device.");
        cJSON_Delete(root);
        return;
    }

    if (add_device(user_id->valuestring, device_id->valuestring) == 0) {
        ESP_LOGI(TAG, "Dodano urządzenie: %s dla użytkownika: %s", device_id->valuestring, user_id->valuestring);
    } else {
        ESP_LOGE(TAG, "Nie udało się dodać urządzenia.");
    }

    cJSON_Delete(root);
}


void handle_add_sensor(const char *data) {
    cJSON *root = cJSON_Parse(data);
    if (!root) {
        ESP_LOGE(TAG, "Błąd parsowania JSON dla add_sensor: %s", data);
        return;
    }

    const cJSON *user_id = cJSON_GetObjectItem(root, "user_id");
    const cJSON *device_id = cJSON_GetObjectItem(root, "device_id");
    const cJSON *sensor_type = cJSON_GetObjectItem(root, "sensor_id");

    if (!cJSON_IsString(user_id) || !cJSON_IsString(device_id) || !cJSON_IsString(sensor_type)) {
        ESP_LOGE(TAG, "Nieprawidłowy format JSON dla add_sensor");
        cJSON_Delete(root);
        return;
    }

    if (add_sensor(user_id->valuestring, device_id->valuestring, sensor_type->valuestring) == 0) {
        ESP_LOGI(TAG, "Dodano czujnik: %s do urządzenia: %s użytkownika: %s", sensor_type->valuestring, device_id->valuestring, user_id->valuestring);
    } else {
        ESP_LOGE(TAG, "Nie udało się dodać czujnika: %s", sensor_type->valuestring);
    }

    cJSON_Delete(root);
}


void handle_add_metric(const char *data) {
    cJSON *root = cJSON_Parse(data);
    if (!root) {
        ESP_LOGE(TAG, "Błąd parsowania JSON dla add_metric: %s", data);
        return;
    }

    const cJSON *user_id = cJSON_GetObjectItem(root, "user_id");
    const cJSON *device_id = cJSON_GetObjectItem(root, "device_id");
    const cJSON *sensor_type = cJSON_GetObjectItem(root, "sensor_id");
    const cJSON *metric = cJSON_GetObjectItem(root, "metric_id");

    if (!cJSON_IsString(user_id) || !cJSON_IsString(device_id) || !cJSON_IsString(sensor_type) || !cJSON_IsString(metric)) {
        ESP_LOGE(TAG, "Nieprawidłowy format JSON dla add_metric");
        cJSON_Delete(root);
        return;
    }

    if (add_metric(user_id->valuestring, device_id->valuestring, sensor_type->valuestring, metric->valuestring) == 0) {
        ESP_LOGI(TAG, "Dodano metrykę: %s do czujnika: %s urządzenia: %s użytkownika: %s",
                 metric->valuestring, sensor_type->valuestring, device_id->valuestring, user_id->valuestring);
    } else {
        ESP_LOGE(TAG, "Nie udało się dodać metryki: %s", metric->valuestring);
    }

    cJSON_Delete(root);
}
