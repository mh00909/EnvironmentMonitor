idf_component_register(SRCS "bmp280_sensor.c"
                    INCLUDE_DIRS "."
                    REQUIRES i2cdev driver bmp280)
