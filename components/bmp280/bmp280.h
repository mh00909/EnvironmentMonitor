#ifndef BMP280_H
#define BMP280_H

#include "esp_err.h"

// Adres domyślny BMP280
#define BMP280_DEFAULT_ADDR 0x76

// Domyślna konfiguracja BMP280
#define BMP280_DEFAULT_CONFIG { \
    .oversampling_temp = BMP280_OSRS_X4, \
    .oversampling_press = BMP280_OSRS_X4, \
    .standby_time = 0x05, \
    .filter = 0x04, \
    .mode = BMP280_NORMAL_MODE \
}

// Tryby pracy czujnika
typedef enum {
    BMP280_SLEEP_MODE = 0x00,
    BMP280_FORCED_MODE = 0x01,
    BMP280_NORMAL_MODE = 0x03
} bmp280_mode_t;

// Opcje oversamplingu
typedef enum {
    BMP280_OSRS_SKIP = 0x00,
    BMP280_OSRS_X1 = 0x01,
    BMP280_OSRS_X2 = 0x02,
    BMP280_OSRS_X4 = 0x03,
    BMP280_OSRS_X8 = 0x04,
    BMP280_OSRS_X16 = 0x05
} bmp280_oversampling_t;

// Struktura konfiguracji BMP280
typedef struct {
    uint8_t oversampling_temp;  // Oversampling dla temperatury
    uint8_t oversampling_press; // Oversampling dla ciśnienia
    uint8_t standby_time;       // Czas standby (rejestr config)
    uint8_t filter;             // Filtr IIR (rejestr config)
    bmp280_mode_t mode;         // Tryb pracy
} bmp280_config_t;

// Struktura stanu BMP280
typedef struct {
    uint8_t i2c_address;        // Adres I2C czujnika
    bmp280_config_t config;     // Aktualna konfiguracja czujnika
} bmp280_state_t;

// Deklaracje funkcji inicjalizacji i konfiguracji

/**
 * @brief Inicjalizuje czujnik BMP280.
 * 
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_init();

/**
 * @brief Resetuje czujnik BMP280.
 * 
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_reset();

/**
 * @brief Ustawia konfigurację BMP280.
 * 
 * @param config Wskaźnik na strukturę z konfiguracją.
 * @return ESP_OK w przypadku sukcesu, ESP_ERR_INVALID_ARG lub ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_apply_config(const bmp280_config_t *config);

/**
 * @brief Odczytuje tryb pracy czujnika.
 * 
 * @return Aktualny tryb pracy (bmp280_mode_t).
 */
bmp280_mode_t bmp280_get_mode();

/**
 * @brief Ustawia tryb pracy czujnika.
 * 
 * @param mode Tryb pracy (bmp280_mode_t).
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_set_mode(bmp280_mode_t mode);

// Deklaracje funkcji dla danych kalibracyjnych i ADC

/**
 * @brief Odczytuje dane kalibracyjne z czujnika BMP280.
 */
void bmp280_read_calibration_data();

/**
 * @brief Odczytuje dane ADC (surowe dane).
 * 
 * @param adc_T Wskaźnik na zmienną do przechowywania surowych danych temperatury.
 * @param adc_P Wskaźnik na zmienną do przechowywania surowych danych ciśnienia.
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_read_adc_values(int32_t *adc_T, int32_t *adc_P);

// Deklaracje funkcji przetwarzania danych

/**
 * @brief Odczytuje skompensowane dane temperatury i ciśnienia.
 * 
 * @param temperature Wskaźnik na zmienną do przechowywania temperatury (w stopniach Celsjusza).
 * @param pressure Wskaźnik na zmienną do przechowywania ciśnienia (w hPa).
 */
void bmp280_read_data(float *temperature, float *pressure);

/**
 * @brief Funkcja kompensująca temperaturę.
 * 
 * @param adc_T Surowa wartość temperatury.
 * @return Skompensowana wartość temperatury (w setnych częściach stopnia Celsjusza).
 */
int32_t bmp280_compensate_temperature(int32_t adc_T);

/**
 * @brief Funkcja kompensująca ciśnienie.
 * 
 * @param adc_P Surowa wartość ciśnienia.
 * @param fine_temp Precyzyjna wartość temperatury (t_fine).
 * @return Skompensowana wartość ciśnienia (w Pa).
 */
uint32_t bmp280_compensate_pressure(int32_t adc_P, int32_t fine_temp);

// Deklaracje funkcji profili i konfiguracji

/**
 * @brief Ustawia profil niskiego zużycia energii.
 * 
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_set_low_power_profile();

/**
 * @brief Ustawia profil wysokiej dokładności.
 * 
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_set_high_accuracy_profile();

/**
 * @brief Ustawia oversampling dla temperatury i ciśnienia.
 * 
 * @param osrs_t Oversampling dla temperatury.
 * @param osrs_p Oversampling dla ciśnienia.
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_set_oversampling(uint8_t osrs_t, uint8_t osrs_p);

/**
 * @brief Konfiguruje czas standby i filtr IIR.
 * 
 * @param t_sb Czas standby.
 * @param filter Filtr IIR.
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_configure(uint8_t t_sb, uint8_t filter);


/**
 * @brief Przelicza ciśnienie na poziom morza.
 *
 * @param pressure Ciśnienie odczytane z BMP280 (w hPa).
 * @param altitude Wysokość nad poziomem morza (w metrach).
 * @return Ciśnienie przeliczone na poziom morza (w hPa).
 */
float bmp280_calculate_sea_level_pressure(float pressure, float altitude);


#endif // BMP280_H
