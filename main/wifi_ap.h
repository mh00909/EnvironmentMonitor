#ifndef WIFI_AP_H
#define WIFI_AP_H

#include <stdbool.h>
#include "esp_http_server.h"




void wifi_init_ap(void);
esp_err_t handle_switch_to_config(httpd_req_t *req);
esp_err_t handle_switch_to_station(httpd_req_t *req);

#endif
