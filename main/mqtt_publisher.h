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
extern float min_temperature_threshold;
extern float max_temperature_threshold;
extern int min_light_threshold;
extern int max_light_threshold;

void mqtt_initialize(void);
void publish_data(esp_mqtt_client_handle_t client, const char *user, const char *device);
void mqtt_stop();
void initialize_mqtt_mutex();
void safe_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data);
void ble_data_task(void *pvParameters);
void set_temperature_range(float min_temp, float max_temp);
void set_light_range(int min_light, int max_light);
void monitor_conditions_task(void *pvParameters);
#endif
