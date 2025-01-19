#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    int power_mode; // 0: Normalny, 1: Oszczędzanie, 2: Deep Sleep
    int deep_sleep_duration; // Czas w sekundach dla deep sleep
} config_esp32;

extern config_esp32 esp32_config;



void save_device_config_to_nvs();
int load_device_config_from_nvs();
void power_manager_task(void *pvParameters);

#endif // POWER_MANAGER_H
