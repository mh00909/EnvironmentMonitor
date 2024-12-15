#include "mqtt_client.h"
#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H
typedef struct {
    const char *user_id;
    const char *device_id;
    esp_mqtt_client_handle_t client_handle; 
} callback_params_t;

extern TaskHandle_t sensor_data_task_handle;
extern esp_mqtt_client_handle_t client_handle;

void mqtt_initialize(void);
void publish_data(esp_mqtt_client_handle_t client, const char *user, const char *device);
void mqtt_stop();
void initialize_mqtt_mutex();
void safe_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data);
#endif
