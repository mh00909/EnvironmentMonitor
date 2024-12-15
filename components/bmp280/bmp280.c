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
    int32_t t_fine; // Precyzyjna wartość temperatury wykorzystywana podczas kompensacji ciśnienia
    uint16_t dig_T1; // Współczynniki kalibracyjne temperatury
    int16_t dig_T2, dig_T3;
    uint16_t dig_P1; // Współczynniki kalibracyjne ciśnienia
    int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
} bmp280_calib_t;

static bmp280_calib_t bmp280_calib; 


esp_err_t bmp280_write_register(uint8_t reg, uint8_t value) {
    ESP_LOGI(TAG, "Zapis do rejestru 0x%02X: wartość = 0x%02X", reg, value);
    esp_err_t err = i2c_write_register(bmp280_state.i2c_address, reg, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Błąd zapisu do rejestru 0x%02X", reg);
    }
    return err;
}


esp_err_t bmp280_read_register(uint8_t reg, uint8_t *data, size_t len) {
    return i2c_read_register(bmp280_state.i2c_address, reg, data, len);
}


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
    if (mode != BMP280_SLEEP_MODE && mode != BMP280_FORCED_MODE && mode != BMP280_NORMAL_MODE) {
        ESP_LOGE(TAG, "Nieprawidłowy tryb pracy: 0x%02X", mode);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t ctrl_meas;

    // Odczytaj aktualny rejestr ctrl_meas (2 najmłodsze bity odpowiadają za tryb pracy czujnika)
    if (bmp280_read_register(0xF4, &ctrl_meas, 1) != ESP_OK) {
        ESP_LOGE(TAG, "Błąd odczytu rejestru ctrl_meas.");
        return ESP_FAIL;
    }


    // Jeśli przejście z NORMAL_MODE do SLEEP_MODE
    if ((ctrl_meas & 0x03) == BMP280_NORMAL_MODE && mode != BMP280_NORMAL_MODE) {
        ctrl_meas = (ctrl_meas & ~0x03) | BMP280_SLEEP_MODE;
        if (bmp280_write_register(0xF4, ctrl_meas) != ESP_OK) {
            ESP_LOGE(TAG, "Błąd ustawiania trybu SLEEP_MODE.");
            return ESP_FAIL;
        }

        // Oczekiwanie na zakończenie pomiarów
        do {
            uint8_t status;
            if (bmp280_read_register(0xF3, &status, 1) != ESP_OK) {
                ESP_LOGE(TAG, "Błąd odczytu rejestru statusu.");
                return ESP_FAIL;
            }
            if (!(status & 0x08)) break; // Bit measuring == 0 oznacza zakończenie pomiaru
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (true);
    }

    // Ustaw nowy tryb pracy
    ctrl_meas = (ctrl_meas & ~0x03) | (mode & 0x03);
    if (bmp280_write_register(0xF4, ctrl_meas) != ESP_OK) {
        ESP_LOGE(TAG, "Błąd ustawiania trybu pracy.");
        return ESP_FAIL;
    }

    // Weryfikacja ustawienia
    uint8_t verify_ctrl_meas;
    if (bmp280_read_register(0xF4, &verify_ctrl_meas, 1) != ESP_OK || verify_ctrl_meas != ctrl_meas) {
        ESP_LOGE(TAG, "Nie udało się zweryfikować trybu pracy. Odczytano: 0x%02X", verify_ctrl_meas);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Tryb pracy ustawiony pomyślnie: 0x%02X", mode);
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
    if (t_sb > 0x07 || filter > 0x07) {
        ESP_LOGE(TAG, "Nieprawidłowe wartości argumentów: t_sb=0x%02X, filter=0x%02X", t_sb, filter);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t config;

    // Ustaw standby time (czas między kolejnymi pomiarami w trybie NORMAL_MODE, bity 5-7) i filtr IIR (redukuje zakłócenia w danych wyjściowych, bity 2-4)
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
    uint8_t ctrl_meas, config_reg;

    if (bmp280_set_mode(BMP280_SLEEP_MODE) != ESP_OK) {
        ESP_LOGE(TAG, "Nie udało się przełączyć BMP280 w tryb SLEEP_MODE.");
        return ESP_FAIL;
    }

    vTaskDelay(pdMS_TO_TICKS(10)); 

    // Konfiguracja oversamplingu i trybu pracy
    ctrl_meas = ((config->oversampling_temp << 5) & 0xE0) |
                ((config->oversampling_press << 2) & 0x1C) |
                (config->mode & 0x03);

    if (bmp280_write_register(0xF4, ctrl_meas) != ESP_OK) {
        ESP_LOGE(TAG, "Nie udało się zapisać rejestru ctrl_meas.");
        return ESP_FAIL;
    }

    // Konfiguracja rejestru config
    config_reg = ((config->standby_time << 5) & 0xE0) |
                 ((config->filter << 2) & 0x1C);

    if (bmp280_write_register(0xF5, config_reg) != ESP_OK) {
        ESP_LOGE(TAG, "Nie udało się zapisać rejestru config.");
        return ESP_FAIL;
    }

    uint8_t verify_ctrl_meas;
    if (bmp280_read_register(0xF4, &verify_ctrl_meas, 1) != ESP_OK ||
        verify_ctrl_meas != ctrl_meas) {
        ESP_LOGE(TAG, "Weryfikacja nieudana. Oczekiwano: 0x%02X, Odczytano: 0x%02X", ctrl_meas, verify_ctrl_meas);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Konfiguracja BMP280 zastosowana pomyślnie.");
    return ESP_OK;
}


bmp280_mode_t bmp280_get_mode() {
    uint8_t ctrl_meas;

    if (bmp280_read_register(0xF4, &ctrl_meas, 1) != ESP_OK) {
        ESP_LOGE(TAG, "Błąd odczytu rejestru ctrl_meas.");
        return BMP280_SLEEP_MODE; 
    }

    bmp280_mode_t mode = (bmp280_mode_t)(ctrl_meas & 0x03);

    // Sprawdź, czy tryb FORCED_MODE przeszedł w SLEEP_MODE
    if (mode == BMP280_FORCED_MODE) {
        uint8_t status;
        bmp280_read_register(0xF3, &status, 1); // Odczytaj status
        if (!(status & 0x08)) {                // Sprawdź bit "measuring"
            ESP_LOGI(TAG, "FORCED_MODE zakończony, przejście do SLEEP_MODE.");
            mode = BMP280_SLEEP_MODE;
        }
    }

    ESP_LOGI(TAG, "Odczytany tryb pracy: %d", mode);
    return mode;
}


uint32_t bmp280_get_measurement_time() {
    uint32_t osrs_t = bmp280_state.config.oversampling_temp;
    uint32_t osrs_p = bmp280_state.config.oversampling_press;

    // Obliczenie czasu w mikrosekundach na podstawie oversampling
    return 1250 + (2300 * osrs_t) + (2300 * osrs_p); // w mikrosekundach
}

esp_err_t bmp280_wait_for_completion() { // wykorzystywane w trybie FORCED_MODE, oczekuje na zakończenie pomiaru
    uint8_t status;
    do {
        if (bmp280_read_register(0xF3, &status, 1) != ESP_OK) {
            ESP_LOGE(TAG, "Błąd odczytu rejestru statusu.");
            return ESP_FAIL;
        }
    } while (status & 0x08); // Czekaj na zakończenie pomiaru (bit measuring)
    return ESP_OK;
}

esp_err_t bmp280_trigger_measurement(float *temperature, float *pressure) {
    // Ustawienie trybu FORCED_MODE
    esp_err_t err = bmp280_set_mode(BMP280_FORCED_MODE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Nie udało się ustawić trybu FORCED_MODE.");
        return err;
    }

    // Poczekaj na zakończenie pomiaru
    err = bmp280_wait_for_completion();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Pomiar w trybie FORCED_MODE nie został ukończony.");
        return err;
    }

    // Odczyt danych
    bmp280_read_data(temperature, pressure);

    return ESP_OK;
}


esp_err_t bmp280_read_adc_values(int32_t *adc_T, int32_t *adc_P) {
    // Czekaj na zakończenie pomiaru
    uint8_t status;
    do {
        bmp280_read_register(0xF3, &status, 1);
    } while (status & 0x08); // Bit measuring

    // Odczytaj dane ADC
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
    if (pressure) *pressure = bmp280_compensate_pressure(adc_P, bmp280_calib.t_fine) / 256.0f ;
}


bool bmp280_is_measuring(void) {
    uint8_t status;
    // Odczytaj rejestr statusu (0xF3)
    esp_err_t err = bmp280_read_register(0xF3, &status, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Nie udało się odczytać rejestru statusu.");
        return false;  // Przy błędzie zakładamy, że pomiar nie jest wykonywany
    }

    // Bit 3 (measuring) wskazuje stan pomiaru
    return (status & 0x08) != 0;
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
float bmp280_calculate_altitude(float pressure, float sea_level_pressure) {
    return 44330.0 * (1.0 - pow(pressure / sea_level_pressure, 0.1903));
}

esp_err_t bmp280_set_low_power_profile() {
    bmp280_config_t config = {
        .oversampling_temp = BMP280_OSRS_X1,
        .oversampling_press = BMP280_OSRS_X1,
        .standby_time = 0x07,
        .filter = 0x00,
        .mode = BMP280_FORCED_MODE
    };
    return bmp280_apply_config(&config);
}

esp_err_t bmp280_set_high_accuracy_profile() {
    bmp280_config_t config = {
        .oversampling_temp = BMP280_OSRS_X16,
        .oversampling_press = BMP280_OSRS_X16,
        .standby_time = 0x00,
        .filter = 0x04,
        .mode = BMP280_NORMAL_MODE
    };
    return bmp280_apply_config(&config);
}
