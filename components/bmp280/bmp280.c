#include "bmp280.h"
#include "i2c_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "BMP280";

// Struktura przechowująca dane kalibracyjne
typedef struct {
    int32_t t_fine; // Precyzyjna wartość temperatury
    uint16_t dig_T1;
    int16_t dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
} bmp280_calib_t;

static bmp280_calib_t bmp280_calib;
static bmp280_state_t bmp280_state = {
    .i2c_address = BMP280_DEFAULT_ADDR,
    .config = BMP280_DEFAULT_CONFIG
};

// Funkcje pomocnicze

static esp_err_t bmp280_write_register(uint8_t reg, uint8_t value) {
    return i2c_write_register(bmp280_state.i2c_address, reg, value);
}

static esp_err_t bmp280_read_register(uint8_t reg, uint8_t *data, size_t len) {
    return i2c_read_register(bmp280_state.i2c_address, reg, data, len);
}

// Funkcje główne 

esp_err_t bmp280_reset() {
    if (bmp280_write_register(0xE0, 0xB6) != ESP_OK) {
        ESP_LOGE(TAG, "Błąd resetu BMP280.");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // Czekaj na zakończenie resetu
    ESP_LOGI(TAG, "Reset BMP280 zakończony.");
    return ESP_OK;
}

esp_err_t bmp280_set_mode(bmp280_mode_t mode) {
    uint8_t ctrl_meas;
    if (bmp280_read_register(0xF4, &ctrl_meas, 1) != ESP_OK) {
        ESP_LOGE(TAG, "Błąd odczytu rejestru ctrl_meas.");
        return ESP_FAIL;
    }
    ctrl_meas = (ctrl_meas & 0xFC) | (mode & 0x03);
    if (bmp280_write_register(0xF4, ctrl_meas) != ESP_OK) {
        ESP_LOGE(TAG, "Błąd zapisu rejestru ctrl_meas.");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Ustawiono tryb pracy BMP280 na: %d", mode);
    return ESP_OK;
}
esp_err_t bmp280_set_oversampling(uint8_t osrs_t, uint8_t osrs_p) {
    uint8_t ctrl_meas;

    // Odczytaj bieżącą konfigurację rejestru ctrl_meas
    esp_err_t err = bmp280_read_register(0xF4, &ctrl_meas, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Nie udało się odczytać rejestru ctrl_meas.");
        return err;
    }

    // Ustaw oversampling dla temperatury (bity 5-7) i ciśnienia (bity 2-4)
    ctrl_meas = (ctrl_meas & 0x03) | ((osrs_t << 5) & 0xE0) | ((osrs_p << 2) & 0x1C);

    // Zapisz nową wartość rejestru ctrl_meas
    err = bmp280_write_register(0xF4, ctrl_meas);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Nie udało się zapisać rejestru ctrl_meas.");
        return err;
    }

    return ESP_OK;
}

esp_err_t bmp280_configure(uint8_t t_sb, uint8_t filter) {
    uint8_t config;

    // Ustaw standby time (bity 5-7) i filtr IIR (bity 2-4)
    config = ((t_sb & 0x07) << 5) | ((filter & 0x07) << 2);

    // Zapisz nową wartość rejestru config (0xF5)
    esp_err_t err = bmp280_write_register(0xF5, config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Nie udało się zapisać rejestru config.");
        return err;
    }

    return ESP_OK;
}

esp_err_t bmp280_apply_config(const bmp280_config_t *config) {
    if (!config) {
        ESP_LOGE(TAG, "Konfiguracja jest NULL.");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err;

    // Ustaw oversampling dla temperatury i ciśnienia
    err = bmp280_set_oversampling(config->oversampling_temp, config->oversampling_press);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Błąd ustawiania oversamplingu.");
        return err;
    }

    // Ustaw czas standby i filtr IIR
    err = bmp280_configure(config->standby_time, config->filter);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Błąd ustawiania czasu standby lub filtra.");
        return err;
    }

    // Ustaw tryb pracy
    err = bmp280_set_mode(config->mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Błąd ustawiania trybu pracy.");
        return err;
    }

    // Aktualizacja stanu globalnego
    bmp280_state.config = *config;

    ESP_LOGI(TAG, "Konfiguracja BMP280 zakończona pomyślnie.");
    return ESP_OK;
}


bmp280_mode_t bmp280_get_mode() {
    uint8_t ctrl_meas;
    if (bmp280_read_register(0xF4, &ctrl_meas, 1) != ESP_OK) {
        ESP_LOGE(TAG, "Błąd odczytu trybu pracy.");
        return BMP280_SLEEP_MODE;
    }
    return (bmp280_mode_t)(ctrl_meas & 0x03);
}

esp_err_t bmp280_read_adc_values(int32_t *adc_T, int32_t *adc_P) {
    uint8_t data[6];
    if (bmp280_read_register(0xF7, data, sizeof(data)) != ESP_OK) {
        ESP_LOGE(TAG, "Błąd odczytu danych ADC.");
        return ESP_FAIL;
    }

    *adc_P = ((int32_t)data[0] << 12) | ((int32_t)data[1] << 4) | (data[2] >> 4);
    *adc_T = ((int32_t)data[3] << 12) | ((int32_t)data[4] << 4) | (data[5] >> 4);
    return ESP_OK;
}

void bmp280_read_calibration_data() {
    uint8_t calib_data[24];
    if (bmp280_read_register(0x88, calib_data, sizeof(calib_data)) != ESP_OK) {
        ESP_LOGE(TAG, "Błąd odczytu danych kalibracyjnych.");
        return;
    }

    bmp280_calib.dig_T1 = (calib_data[1] << 8) | calib_data[0];
    bmp280_calib.dig_T2 = (calib_data[3] << 8) | calib_data[2];
    bmp280_calib.dig_T3 = (calib_data[5] << 8) | calib_data[4];
    bmp280_calib.dig_P1 = (calib_data[7] << 8) | calib_data[6];
    bmp280_calib.dig_P2 = (calib_data[9] << 8) | calib_data[8];
    bmp280_calib.dig_P3 = (calib_data[11] << 8) | calib_data[10];
    bmp280_calib.dig_P4 = (calib_data[13] << 8) | calib_data[12];
    bmp280_calib.dig_P5 = (calib_data[15] << 8) | calib_data[14];
    bmp280_calib.dig_P6 = (calib_data[17] << 8) | calib_data[16];
    bmp280_calib.dig_P7 = (calib_data[19] << 8) | calib_data[18];
    bmp280_calib.dig_P8 = (calib_data[21] << 8) | calib_data[20];
    bmp280_calib.dig_P9 = (calib_data[23] << 8) | calib_data[22];
}

int32_t bmp280_compensate_temperature(int32_t adc_T) {
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)bmp280_calib.dig_T1 << 1))) * (int32_t)bmp280_calib.dig_T2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)bmp280_calib.dig_T1) * 
                      ((adc_T >> 4) - (int32_t)bmp280_calib.dig_T1)) >> 12) * 
                    (int32_t)bmp280_calib.dig_T3) >> 14;
    bmp280_calib.t_fine = var1 + var2;
    return (bmp280_calib.t_fine * 5 + 128) >> 8;
}

uint32_t bmp280_compensate_pressure(int32_t adc_P, int32_t fine_temp) {
    int64_t var1 = (int64_t)fine_temp - 128000;
    int64_t var2 = var1 * var1 * (int64_t)bmp280_calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)bmp280_calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)bmp280_calib.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)bmp280_calib.dig_P3) >> 8) + ((var1 * (int64_t)bmp280_calib.dig_P2) << 12);
    var1 = (((int64_t)1 << 47) + var1) * (int64_t)bmp280_calib.dig_P1 >> 33;

    if (var1 == 0) return 0; // Uniknięcie dzielenia przez zero

    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)bmp280_calib.dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)bmp280_calib.dig_P8 * p) >> 19;

    return (uint32_t)(((p + var1 + var2) >> 8) + ((int64_t)bmp280_calib.dig_P7 << 4));
}

void bmp280_read_data(float *temperature, float *pressure) {
    int32_t adc_T, adc_P;
    if (bmp280_read_adc_values(&adc_T, &adc_P) != ESP_OK) {
        ESP_LOGE(TAG, "Błąd odczytu danych ADC.");
        if (temperature) *temperature = 0.0;
        if (pressure) *pressure = 0.0;
        return;
    }
    if (temperature) *temperature = bmp280_compensate_temperature(adc_T) / 100.0f;
    if (pressure) *pressure = bmp280_compensate_pressure(adc_P, bmp280_calib.t_fine) / 256.0f;
}

esp_err_t bmp280_init() {
    if (bmp280_reset() != ESP_OK) return ESP_FAIL;

    uint8_t id = 0;
    if (bmp280_read_register(0xD0, &id, 1) != ESP_OK || id != 0x58) {
        ESP_LOGE(TAG, "Nieprawidłowe ID czujnika BMP280. Odczytane: 0x%02X", id);
        return ESP_FAIL;
    }

    bmp280_read_calibration_data();
    return bmp280_apply_config(&bmp280_state.config);
}


float bmp280_calculate_sea_level_pressure(float pressure, float altitude) {
    return pressure / pow(1.0 - (altitude / 44330.0), 5.255);
}