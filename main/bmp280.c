#include "bmp280.h"
#include "i2c_driver.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BMP280";
float temp_c = 0.0;
float press_pa = 0.0;


// Struktura przechowująca dane kalibracyjne i stan czujnika
typedef struct {
    int32_t t_fine; // Zmienna do przechowywania temperatury
    uint16_t dig_T1;
    int16_t dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
} bmp280_t;

bmp280_t bmp280;

void bmp280_read_calibration_data() {
    uint8_t calib_data[24];
    i2c_read_register(BMP280_ADDR, 0x88, calib_data, 24);

    bmp280.dig_T1 = (calib_data[1] << 8) | calib_data[0];
    bmp280.dig_T2 = (calib_data[3] << 8) | calib_data[2];
    bmp280.dig_T3 = (calib_data[5] << 8) | calib_data[4];
    bmp280.dig_P1 = (calib_data[7] << 8) | calib_data[6];
    bmp280.dig_P2 = (calib_data[9] << 8) | calib_data[8];
    bmp280.dig_P3 = (calib_data[11] << 8) | calib_data[10];
    bmp280.dig_P4 = (calib_data[13] << 8) | calib_data[12];
    bmp280.dig_P5 = (calib_data[15] << 8) | calib_data[14];
    bmp280.dig_P6 = (calib_data[17] << 8) | calib_data[16];
    bmp280.dig_P7 = (calib_data[19] << 8) | calib_data[18];
    bmp280.dig_P8 = (calib_data[21] << 8) | calib_data[20];
    bmp280.dig_P9 = (calib_data[23] << 8) | calib_data[22];
}


esp_err_t bmp280_init() {
    

    uint8_t id = 0;

    // Reset czujnika
    i2c_write_register(BMP280_ADDR, 0xE0, 0xB6);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Sprawdzenie identyfikatora
    if (i2c_read_register(BMP280_ADDR, 0xD0, &id, 1) != ESP_OK || id != 0x58) {
        ESP_LOGE(TAG, "BMP280 initialization failed. ID=0x%02X", id);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "BMP280 detected, ID=0x%02X", id);

    // Odczyt danych kalibracyjnych
    uint8_t calib_data[24];
    if (i2c_read_register(BMP280_ADDR, 0x88, calib_data, 24) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration data");
        return ESP_FAIL;
    }

    bmp280.dig_T1 = (calib_data[1] << 8) | calib_data[0];
    bmp280.dig_T2 = (calib_data[3] << 8) | calib_data[2];
    bmp280.dig_T3 = (calib_data[5] << 8) | calib_data[4];
    bmp280.dig_P1 = (calib_data[7] << 8) | calib_data[6];
    bmp280.dig_P2 = (calib_data[9] << 8) | calib_data[8];
    bmp280.dig_P3 = (calib_data[11] << 8) | calib_data[10];
    bmp280.dig_P4 = (calib_data[13] << 8) | calib_data[12];
    bmp280.dig_P5 = (calib_data[15] << 8) | calib_data[14];
    bmp280.dig_P6 = (calib_data[17] << 8) | calib_data[16];
    bmp280.dig_P7 = (calib_data[19] << 8) | calib_data[18];
    bmp280.dig_P8 = (calib_data[21] << 8) | calib_data[20];
    bmp280.dig_P9 = (calib_data[23] << 8) | calib_data[22];
    ESP_LOGI(TAG, "Calibration data loaded");

    // Konfiguracja czujnika
    
    esp_err_t ret = i2c_write_register(BMP280_ADDR, 0xF4, 0xB7);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure 0xF4: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_write_register(BMP280_ADDR, 0xF5, 0xA0); // Filtr x16, czas standby 500 ms

    ESP_LOGI(TAG, "Calibration Data: T1=%u, T2=%d, T3=%d", bmp280.dig_T1, bmp280.dig_T2, bmp280.dig_T3);
ESP_LOGI(TAG, "Calibration Data: P1=%u, P2=%d, P3=%d", bmp280.dig_P1, bmp280.dig_P2, bmp280.dig_P3);
ESP_LOGI(TAG, "Calibration Data: P4=%d, P5=%d, P6=%d", bmp280.dig_P4, bmp280.dig_P5, bmp280.dig_P6);
ESP_LOGI(TAG, "Calibration Data: P7=%d, P8=%d, P9=%d", bmp280.dig_P7, bmp280.dig_P8, bmp280.dig_P9);

    return ESP_OK;
}


int32_t bmp280_compensate_temperature(int32_t adc_T) {
    int32_t var1, var2;

    var1 = ((((adc_T >> 3) - ((int32_t)bmp280.dig_T1 << 1))) * ((int32_t)bmp280.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)bmp280.dig_T1)) * ((adc_T >> 4) - ((int32_t)bmp280.dig_T1))) >> 12) * ((int32_t)bmp280.dig_T3)) >> 14;

    bmp280.t_fine = var1 + var2;
    return (bmp280.t_fine * 5 + 128) >> 8;
}


uint32_t bmp280_compensate_pressure(int32_t adc_P, int32_t fine_temp) {
    int64_t var1, var2, p;

    // Obliczenie var1 i var2
    var1 = (int64_t)fine_temp - 128000;
    var2 = var1 * var1 * (int64_t)bmp280.dig_P6;
    var2 = var2 + ((var1 * (int64_t)bmp280.dig_P5) << 17);
    var2 = var2 + (((int64_t)bmp280.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)bmp280.dig_P3) >> 8) + ((var1 * (int64_t)bmp280.dig_P2) << 12);
    var1 = (((int64_t)1 << 47) + var1) * (int64_t)bmp280.dig_P1 >> 33;

    // Uniknięcie dzielenia przez zero
    if (var1 == 0) {
        ESP_LOGE("BMP280", "Division by zero in pressure calculation.");
        return 0;
    }

    // Kompensacja ciśnienia
    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;

    // Korekta końcowa
    var1 = ((int64_t)bmp280.dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)bmp280.dig_P8 * p) >> 19;

    p = ((p + var1 + var2) >> 8) + ((int64_t)bmp280.dig_P7 << 4);
    return (uint32_t)p; // Zwracanie w Pa
}



void bmp280_read_data() {
    uint8_t data[6];
    i2c_read_register(BMP280_ADDR, 0xF7, data, 6);

    int32_t adc_P = (data[0] << 12) | (data[1] << 4) | (data[2] >> 4);
    int32_t adc_T = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);

    int32_t temperature = bmp280_compensate_temperature(adc_T);
    uint32_t pressure = bmp280_compensate_pressure(adc_P, bmp280.t_fine);


    temp_c = temperature / 100.0;
    press_pa = pressure / 256.0;

    ESP_LOGI("BMP280", "Temperature: %.2f °C, Pressure: %.2f hPa",
             temp_c, press_pa);
}


