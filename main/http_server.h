#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "esp_http_server.h"
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

#endif // HTTP_SERVER_H
