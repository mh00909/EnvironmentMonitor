#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "esp_http_server.h"
#include "bmp280.h"
#include "power_manager.h"
#include <stdbool.h>


extern bool is_config_mode;
extern httpd_handle_t server;
extern TaskHandle_t config_task_handle;
extern bool config_completed;


httpd_handle_t start_webserver(void);
void stop_webserver(httpd_handle_t *server);
void register_endpoints(httpd_handle_t server);
esp_err_t handle_set_wifi_post(httpd_req_t *req);
esp_err_t handle_bmp280_config_post(httpd_req_t *req);
esp_err_t handle_bmp280_config_get(httpd_req_t *req);

esp_err_t handle_switch_to_station(httpd_req_t *req);
void save_bmp280_config_to_nvs(bmp280_config_t *config);
void load_bmp280_config_from_nvs(bmp280_config_t *config);

esp_err_t handle_set_mqtt_post(httpd_req_t *req);
void save_mqtt_config_to_nvs(const char* broker, int port, const char* user, const char* password);
void load_mqtt_config_from_nvs(char* broker, size_t broker_len, int* port, char* user, size_t user_len, char* password, size_t password_len);

esp_err_t handle_power_config_get(httpd_req_t *req);
esp_err_t handle_power_config_post(httpd_req_t *req);
esp_err_t handle_complete_config(httpd_req_t *req);
#endif // HTTP_SERVER_H
