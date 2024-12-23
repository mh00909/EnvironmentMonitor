/**
 * @file bmp280.h
 * Plik nagłówkowy zawierający deklaracje funkcji i struktur używanych do obsługi czujnika BMP280 
 * przy wykorzystaniu magistrali I2C.
 */
#ifndef BMP280_H
#define BMP280_H

#include "esp_err.h"
#include <stdbool.h>

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

extern float current_temperature_bmp280;
extern float current_pressure_bmp280;

/**
 * Tryby pracy czujnika BMP280.
 */
typedef enum {
    BMP280_SLEEP_MODE = 0x00,    ///< Tryb uśpienia, minimalne zużycie energii
    BMP280_FORCED_MODE = 0x01,   ///< Tryb wymuszonej pracy, pojedynczy pomiar
    BMP280_NORMAL_MODE = 0x03    ///< Tryb normalny, ciągłe pomiary
} bmp280_mode_t;

/**
 * Opcje oversamplingu dla BMP280.
 */
typedef enum {
    BMP280_OSRS_SKIP = 0x00,     ///< Brak oversamplingu
    BMP280_OSRS_X1 = 0x01,       ///< Jednokrotny oversampling
    BMP280_OSRS_X2 = 0x02,       ///< Dwukrotny oversampling
    BMP280_OSRS_X4 = 0x03,       ///< Czterokrotny oversampling
    BMP280_OSRS_X8 = 0x04,       ///< Ośmiokrotny oversampling
    BMP280_OSRS_X16 = 0x05       ///< Szesnastokrotny oversampling
} bmp280_oversampling_t;

/**
 * Struktura konfiguracji BMP280
 */
typedef struct {
    uint8_t oversampling_temp;  ///< Oversampling dla temperatury
    uint8_t oversampling_press; ///< Oversampling dla ciśnienia
    uint8_t standby_time;       ///< Czas standby w rejestrze config
    uint8_t filter;             ///< Filtr IIR w rejestrze config
    bmp280_mode_t mode;         ///< Aktualny tryb pracy
} bmp280_config_t;

/**
 * Struktura stanu BMP280
 */
typedef struct {
    uint8_t i2c_address;        ///< Adres I2C czujnika BMP280
    bmp280_config_t config;     ///< Aktualna konfiguracja czujnika
} bmp280_state_t;


/**
 * Przechowuje aktualny stan i konfigurację czujnika
 */
static bmp280_state_t bmp280_state = {
    .i2c_address = BMP280_DEFAULT_ADDR, // 0x76
    .config = BMP280_DEFAULT_CONFIG
};



/**
 * Inicjalizuje czujnik BMP280.
 * Funkcja ustawia domyślną konfigurację czujnika i przygotowuje go do pracy.
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_init();

/**
 * Resetuje czujnik BMP280.
 * Funkcja wykonuje reset urządzenia, przywracając ustawienia domyślne.
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_reset();

/**
 * Ustawia konfigurację BMP280.
 * Funkcja przyjmuje strukturę konfiguracji, która określa parametry działania czujnika.
 * @param config Wskaźnik na strukturę z konfiguracją.
 * @return ESP_OK w przypadku sukcesu, ESP_ERR_INVALID_ARG lub ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_apply_config(const bmp280_config_t *config);

/**
 * Odczytuje tryb pracy czujnika.
 * Funkcja zwraca aktualny tryb pracy czujnika, umożliwiając jego diagnostykę.
 * @return Aktualny tryb pracy (bmp280_mode_t).
 */
bmp280_mode_t bmp280_get_mode();

/**
 * Oblicza czas potrzebny na wykonanie pomiaru.
 * Funkcja zwraca czas potrzebny na wykonanie pomiaru w zależności od oversamplingu.
 * @return Czas pomiaru w mikrosekundach.
 */
uint32_t bmp280_get_measurement_time();

/**
 * Oczekuje na zakończenie pomiaru.
 * Funkcja sprawdza bit "measuring" w rejestrze statusu i czeka na jego wyzerowanie.
 * @return ESP_OK w przypadku zakończenia pomiaru, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_wait_for_completion();

/**
 * Rozpoczyna pomiar i zwraca wyniki.
 * Funkcja przełącza czujnik w tryb wymuszony, czeka na zakończenie pomiaru, 
 * a następnie zwraca wyniki w odpowiednich zmiennych.
 * @param temperature Wskaźnik na zmienną, w której zostanie zapisany wynik pomiaru temperatury (°C).
 * @param pressure Wskaźnik na zmienną, w której zostanie zapisany wynik pomiaru ciśnienia (Pa).
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_trigger_measurement(float *temperature, float *pressure);


/**
 * Sprawdza, czy czujnik wykonuje pomiar.
 * Funkcja odczytuje status czujnika i zwraca informacje, czy bit "measuring" jest aktywny.
 * @return true, jeśli pomiar jest w toku, false w przeciwnym wypadku.
 */
bool bmp280_is_measuring(void);


/**
 * Ustawia tryb pracy czujnika.
 * Funkcja pozwala na przełączenie trybu pracy czujnika
 * @param mode Tryb pracy (bmp280_mode_t).
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_set_mode(bmp280_mode_t mode);


/**
 * Odczytuje dane kalibracyjne z czujnika BMP280.
 * Funkcja pobiera dane kalibracyjne niezbędne do przetwarzania surowych wyników pomiarów.
 */
void bmp280_read_calibration_data();

/**
 * Odczytuje dane ADC (surowe dane).
 * Funkcja odczytuje dane ADC bez przetwarzania, które można wykorzystać do kalibracji.
 * @param adc_T Wskaźnik na zmienną do przechowywania surowych danych temperatury.
 * @param adc_P Wskaźnik na zmienną do przechowywania surowych danych ciśnienia.
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_read_adc_values(int32_t *adc_T, int32_t *adc_P);



/**
 * Odczytuje skompensowane dane temperatury i ciśnienia.
 * Funkcja przetwarza dane surowe, zwracając wyniki w odpowiednich jednostkach.
 * @param temperature Wskaźnik na zmienną do przechowywania temperatury (w stopniach Celsjusza).
 * @param pressure Wskaźnik na zmienną do przechowywania ciśnienia (w Pa).
 */
void bmp280_read_data(float *temperature, float *pressure);

/**
 * Funkcja kompensująca temperaturę.
 * @param adc_T Surowa wartość temperatury.
 * @return Skompensowana wartość temperatury (w setnych częściach stopnia Celsjusza).
 */
int32_t bmp280_compensate_temperature(int32_t adc_T);

/**
 * Funkcja kompensująca ciśnienie.
 * @param adc_P Surowa wartość ciśnienia.
 * @param fine_temp Precyzyjna wartość temperatury (t_fine).
 * @return Skompensowana wartość ciśnienia (w Pa).
 */
uint32_t bmp280_compensate_pressure(int32_t adc_P, int32_t fine_temp);



/**
 * Ustawia profil niskiego zużycia energii.
 * Funkcja zmniejsza częstotliwość pomiarów, co zmniejsza zużycie energii.
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_set_low_power_profile();


/**
 * Ustawia profil wysokiej dokładności.
 * Funkcja zwiększa dokładność pomiarów, wykorzystując wysoki oversampling.
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_set_high_accuracy_profile();

/**
 * Ustawia oversampling dla temperatury i ciśnienia.
 * @param osrs_t Oversampling dla temperatury.
 * @param osrs_p Oversampling dla ciśnienia.
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_set_oversampling(uint8_t osrs_t, uint8_t osrs_p);

/**
 * Konfiguruje czas standby i filtr IIR.
 * Funkcja pozwala na dostosowanie czasu oczekiwania i konfiguracji filtra.
 * @param t_sb Czas standby.
 * @param filter Filtr IIR.
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_configure(uint8_t t_sb, uint8_t filter);

/**
 * Odczytuje rejestr z czujnika BMP280.
 * Funkcja umożliwia odczyt danych z określonego rejestru.
 * @param reg Adres rejestru do odczytu.
 * @param data Wskaźnik na bufor do przechowywania odczytanych danych.
 * @param len Liczba bajtów do odczytu.
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_read_register(uint8_t reg, uint8_t *data, size_t len);
/**
 * Zapisuje dane do rejestru BMP280.
 * Funkcja umożliwia zapis danych do określonego rejestru.
 * @param reg Adres rejestru do zapisu.
 * @param value Wartość do zapisania w rejestrze.
 * @return ESP_OK w przypadku sukcesu, ESP_FAIL w przypadku błędu.
 */
esp_err_t bmp280_write_register(uint8_t reg, uint8_t value);


/**
 * Przelicza ciśnienie na poziom morza.
 * Funkcja przelicza odczytane ciśnienie na poziom odniesienia przy podanej wysokości.
 * @param pressure Ciśnienie odczytane z BMP280 (w Pa).
 * @param altitude Wysokość nad poziomem morza (w metrach).
 * @return Ciśnienie przeliczone na poziom morza (w Pa).
 */
float bmp280_calculate_sea_level_pressure(float pressure, float altitude);

/**
 * Przelicza ciśnienie na wysokość nad poziomem morza.
 * @param pressure bieżące ciśnienie atmosferyczne (w hPa)
 * @param sea_level_pressure Ciśnienie atmosferyczne na poziomie morza w hPa.
 * @return Wysokość nad poziomem morza w metrach.
 */
float bmp280_calculate_altitude(float pressure, float sea_level_pressure);



#endif // BMP280_H
