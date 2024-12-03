#ifndef WIFI_STATION_H
#define WIFI_STATION_H

#include <stdbool.h>

extern bool wifi_connected; 
extern bool is_config_mode;

void wifi_init_sta(void);
void connect_to_wifi() ;
void connect_to_wifi_task(void *pvParameter);
void save_wifi_credentials_to_nvs(const char* ssid, const char* password);
#endif
