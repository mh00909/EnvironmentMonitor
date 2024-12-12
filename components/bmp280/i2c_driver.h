#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#include "esp_err.h"

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SCL_IO 22
#define I2C_MASTER_SDA_IO 21
#define I2C_MASTER_FREQ_HZ 50000

void i2c_master_init(void);
void i2c_scan(void);
esp_err_t i2c_write_register(uint8_t device_addr, uint8_t reg_addr, uint8_t data);
esp_err_t i2c_read_register(uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t len);

#endif // I2C_DRIVER_H
