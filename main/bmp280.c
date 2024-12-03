#include "bmp280.h"
#include "i2c_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/FreeRTOS.h"

#define BMP280_ADDR 0x76

static const char *TAG = "BMP280";
static uint16_t dig_T1;
static int16_t dig_T2, dig_T3;
static uint16_t dig_P1;
static int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;

void bmp280_init() {
    i2c_write_register(BMP280_ADDR, 0xE0, 0xB6); // Reset command
vTaskDelay(pdMS_TO_TICKS(10)); // Krótkie opóźnienie


    uint8_t id = 0;
    if (i2c_read_register(BMP280_ADDR, 0xD0, &id, 1) != ESP_OK || id != 0x58) {
        ESP_LOGE(TAG, "Failed to detect BMP280 or invalid ID: 0x%02X", id);
        return;
    }
    ESP_LOGI(TAG, "BMP280 detected, ID=0x%x", id);

    // Load calibration data
    uint8_t calib_data[24];
    if (i2c_read_register(BMP280_ADDR, 0x88, calib_data, 24) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration data");
        return;
    }

    dig_T1 = (calib_data[1] << 8) | calib_data[0];
    dig_T2 = (calib_data[3] << 8) | calib_data[2];
    dig_T3 = (calib_data[5] << 8) | calib_data[4];
    dig_P1 = (calib_data[7] << 8) | calib_data[6];
    dig_P2 = (calib_data[9] << 8) | calib_data[8];
    dig_P3 = (calib_data[11] << 8) | calib_data[10];
    dig_P4 = (calib_data[13] << 8) | calib_data[12];
    dig_P5 = (calib_data[15] << 8) | calib_data[14];
    dig_P6 = (calib_data[17] << 8) | calib_data[16];
    dig_P7 = (calib_data[19] << 8) | calib_data[18];
    dig_P8 = (calib_data[21] << 8) | calib_data[20];
    dig_P9 = (calib_data[23] << 8) | calib_data[22];

    ESP_LOGI(TAG, "Calibration data loaded");

    // Configure BMP280 (normal mode, oversampling settings)
    i2c_write_register(BMP280_ADDR, 0xF4, 0x27); // Temperature and pressure oversampling x1, normal mode
    i2c_write_register(BMP280_ADDR, 0xF5, 0xA0); // Filter and standby time
}


int32_t t_fine;

int32_t bmp280_compensate_temperature(int32_t adc_T) {
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) *
                   ((int32_t)dig_T3)) >> 14;
    t_fine = var1 + var2;
    return (t_fine * 5 + 128) >> 8; // Temperature in °C * 100
}

uint32_t bmp280_compensate_pressure(int32_t adc_P) {
    int64_t var1 = ((int64_t)t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)dig_P6;
    var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
    var2 = var2 + (((int64_t)dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;

    if (var1 == 0) {
        return 0; // avoid division by zero
    }
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)dig_P8) * p) >> 19;

    p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);
    return (uint32_t)p; // Pressure in Pa
}

void bmp280_read_data() {
    uint8_t data[6];
    if (i2c_read_register(BMP280_ADDR, 0xF7, data, 6) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read data");
        return;
    }

    int32_t adc_P = (data[0] << 12) | (data[1] << 4) | (data[2] >> 4);
    int32_t adc_T = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);

    int32_t temperature = bmp280_compensate_temperature(adc_T);
    uint32_t pressure = bmp280_compensate_pressure(adc_P);

    ESP_LOGI(TAG, "Temperature: %.2f °C, Pressure: %.2f hPa",
             temperature / 100.0, pressure / 100.0);
}
void bmp280_test_id() {
    uint8_t id = 0;
    esp_err_t ret = i2c_read_register(0x76, 0xD0, &id, 1);
    if (ret == ESP_OK) {
        ESP_LOGI("BMP280_TEST", "Detected BMP280, ID=0x%02X", id);
    } else {
        ESP_LOGE("BMP280_TEST", "Failed to read BMP280 ID, error: %s", esp_err_to_name(ret));
    }
}
