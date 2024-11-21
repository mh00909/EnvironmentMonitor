#include "mqtt_client.h"
#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

void mqtt_initialize(void);
void publish_data(esp_mqtt_client_handle_t client, const char *user, const char *device);

#endif
