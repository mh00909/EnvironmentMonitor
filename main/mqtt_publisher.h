#include "mqtt_client.h"
#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H




#define MAX_USERS 5
#define MAX_DEVICES 3
#define MAX_SENSORS 5
#define MAX_METRICS 6

typedef struct {
    char metric[50]; // Nazwa metryki, np. "temperature", "pressure"
} metric_t;

typedef struct {
    char sensor_type[50]; // Typ sensora, np. "bmp280", "photoresistor"
    metric_t metrics[MAX_METRICS]; // Metryki sensora
    int metric_count; // Liczba metryk
} sensor_t;

typedef struct {
    char device_id[50]; // ID urządzenia, np. "device1"
    sensor_t sensors[MAX_SENSORS]; // Sensory urządzenia
    int sensor_count; // Liczba sensorów
} device_t;

typedef struct {
    char user_id[50]; // ID użytkownika, np. "user1"
    device_t devices[MAX_DEVICES]; // Urządzenia użytkownika
    int device_count; // Liczba urządzeń
} user_t;

extern user_t users[MAX_USERS]; // Lista użytkowników
extern int user_count; // Liczba użytkowników




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
//void publish_data(esp_mqtt_client_handle_t client, const char *user, const char *device);
void mqtt_stop();
void initialize_mqtt_mutex();
//void safe_publish(client_info_t *client, const char *topic, const char *data);

void ble_data_task(void *pvParameters);
void set_temperature_range(float min_temp, float max_temp);
void set_light_range(int min_light, int max_light);
void monitor_conditions_task(void *pvParameters);
void restart_mqtt_client();


int add_client(const char *user_id, const char *device_id, const char *topics[], int topic_count);
void remove_client(const char *user_id, const char *device_id);

void publish_data_for_all_clients();

typedef void (*mqtt_handler_t)(const char *topic, const char *data);
void generate_mqtt_topic(char *topic, size_t topic_size, const char *user_id, const char *device_id, const char *sensor_type, const char *metric);

void handle_add_user(const char *data);
void handle_add_device(const char *data) ;
void handle_add_sensor(const char *data);
void handle_add_metric(const char *data);



int add_user(const char *user_id);
int add_device(const char *user_id, const char *device_id);
int add_sensor(const char *user_id, const char *device_id, const char *sensor_type);
int add_metric(const char *user_id, const char *device_id, const char *sensor_type, const char *metric);

void safe_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data);

void publish_all_metrics(const char *user_id, const char *device_id);
void subscribe_all_topics(const char *user_id);
void subscribe_all_users();

void publish_ble_data(const char *user, const char *device);
void publish_light_sensor_data(const char *user, const char *device);
void publish_bmp280_data(const char *user, const char *device);

void save_light_range_to_nvs(int min_light, int max_light);
void save_temperature_range_to_nvs(float min_temp, float max_temp);
void load_light_range_from_nvs(int *min_light, int *max_light);
void load_temperature_range_from_nvs(float *min_temp, float *max_temp);
#endif