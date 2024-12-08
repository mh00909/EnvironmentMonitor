#ifndef BMP280_H
#define BMP280_H

#include "esp_err.h"

#define BMP280_ADDR 0x76

extern float temp_c;
extern float  press_pa;

esp_err_t bmp280_init(void);
void bmp280_read_data(void);
void bmp280_test_id(void);
int32_t bmp280_compensate_temperature(int32_t adc_T);
uint32_t bmp280_compensate_pressure(int32_t adc_P, int32_t fine_temp);
void bmp280_read_calibration_data();
#endif // BMP280_H
