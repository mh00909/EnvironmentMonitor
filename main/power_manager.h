#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef enum {
    MODE_STANDARD,
    MODE_POWER_SAVE
} PowerMode;

void set_power_mode(PowerMode mode);
void power_manager_task(void *pvParameters);

#endif // POWER_MANAGER_H
