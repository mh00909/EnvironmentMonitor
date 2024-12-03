#ifndef WIFI_AP_H
#define WIFI_AP_H

#include <stdbool.h>
#include "esp_http_server.h"


extern bool is_config_mode;
extern httpd_handle_t server;
extern TaskHandle_t config_task_handle;

void wifi_init_ap(void);
httpd_handle_t start_webserver(void);
void stop_webserver(httpd_handle_t server);

esp_err_t handle_switch_to_config(httpd_req_t *req);
esp_err_t handle_switch_to_station(httpd_req_t *req);

#endif
