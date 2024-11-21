#ifndef BMP280_SENSOR_H
#define BMP280_SENSOR_H

#include "esp_err.h"

esp_err_t bmp280_init_sensor(void); 
void i2c_master_init();

#endif // BMP280_SENSOR_H
