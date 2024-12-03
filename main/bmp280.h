#ifndef BMP280_H
#define BMP280_H

#include "esp_err.h"

void bmp280_init(void);
void bmp280_read_data(void);
void bmp280_test_id(void);
#endif // BMP280_H
