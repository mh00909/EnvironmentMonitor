#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>


typedef struct {
    float temperature;     
    float pressure;       
    float humidity;    
    uint16_t light_level; 
} SensorData;

void all_sensors_init(void);
void sensor_get_all_data(SensorData *data);


#endif // SENSORS_H
