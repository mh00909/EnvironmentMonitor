#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "esp_http_server.h"
#include "bmp280.h"
#include <stdbool.h>


extern bool is_config_mode;
extern httpd_handle_t server;
extern TaskHandle_t config_task_handle;



httpd_handle_t start_webserver(void);
void stop_webserver(httpd_handle_t server);
void register_endpoints(httpd_handle_t server);
esp_err_t websocket_handler(httpd_req_t *req);
esp_err_t handle_set_wifi_post(httpd_req_t *req);
void notify_clients(httpd_handle_t server, const char *message);
static esp_err_t handle_bmp280_config_post(httpd_req_t *req);
static esp_err_t handle_bmp280_config_get(httpd_req_t *req);
static esp_err_t handle_options(httpd_req_t *req) ;
esp_err_t handle_switch_to_station(httpd_req_t *req);
void save_bmp280_config_to_nvs(bmp280_config_t *config);
void load_bmp280_config_from_nvs(bmp280_config_t *config);
#endif // HTTP_SERVER_H
