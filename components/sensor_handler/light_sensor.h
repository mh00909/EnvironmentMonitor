#ifndef LIGHT_SENSOR_H
#define LIGHT_SENSOR_H

#include <stdint.h>

#define ADC_MAX_VALUE 4095.0
#define V_REF 3.3
#define R_PULLDOWN 10000.0 // w Ohmach
#define A 500 // Współczynnik kalibracji
#define N 0.7 // Wykładnik zależności

#define LIGHT_SENSOR_ADC_CHANNEL ADC1_CHANNEL_5 // GPIO 33 
#define LED1_GPIO 19
#define LED2_GPIO 32

extern int current_light;

void light_sensor_init(void);
void light_sensor_read(int* light);
float light_sensor_to_percentage(uint16_t adc_value);

#endif // LIGHT_SENSOR_H
