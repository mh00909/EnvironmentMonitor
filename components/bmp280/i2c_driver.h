/**
 * @file i2c_driver.h
 * @brief Nagłówek dla obsługi magistrali I2C w trybie master.
 * 
 * Zawiera definicje i deklaracje funkcji dla inicjalizacji magistrali I2C, 
szukanie urządzeń na magistrali oraz operacje zapisu i odczytu rejestrów.
 */

#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#include "esp_err.h"

/**
 * @brief Numer portu I2C master.
 */
#define I2C_MASTER_NUM I2C_NUM_0

/**
 * @brief GPIO dla sygnału SCL (zegar I2C).
 */
#define I2C_MASTER_SCL_IO 22

/**
 * @brief GPIO dla sygnału SDA (dane I2C).
 */
#define I2C_MASTER_SDA_IO 21

/**
 * @brief Częstotliwość zegara I2C w Hz.
 */
#define I2C_MASTER_FREQ_HZ 50000

/**
 * @brief Inicjalizuje magistralę I2C w trybie master.
 * 
 * Funkcja konfiguruje magistralę I2C z ustalonymi parametrami, takimi jak piny SDA, SCL oraz częstotliwość zegara.
 * 
 * @note Funkcja musi być wywołana przed jakąkolwiek operacją na magistrali I2C.
 */
void i2c_master_init(void);

/**
 * @brief Skanuje magistralę I2C w poszukiwaniu urządzeń.
 * 
 * Funkcja sprawdza wszystkie adresy od 1 do 127, próbując nawiązać komunikację. Jeśli urządzenie odpowie,
 * wyświetlany jest jego adres.
 * 
 * @note Przydatne do diagnostyki i wykrywania urządzeń na magistrali.
 */
void i2c_scan(void);

/**
 * @brief Zapisuje pojedynczy bajt danych do rejestru urządzenia I2C.
 * 
 * @param device_addr Adres urządzenia I2C.
 * @param reg_addr Adres rejestru w urządzeniu.
 * @param data Dane do zapisania.
 * @return ESP_OK w przypadku sukcesu, inny kod błędu w przypadku niepowodzenia.
 * 
 * @note Funkcja zakłada, że magistrala I2C została wcześniej zainicjalizowana.
 */
esp_err_t i2c_write_register(uint8_t device_addr, uint8_t reg_addr, uint8_t data);

/**
 * @brief Odczytuje dane z rejestru urządzenia I2C.
 * 
 * @param device_addr Adres urządzenia I2C.
 * @param reg_addr Adres rejestru do odczytu.
 * @param data Bufor, do którego zostaną zapisane odczytane dane.
 * @param len Liczba bajtów do odczytania.
 * @return ESP_OK w przypadku sukcesu, inny kod błędu w przypadku niepowodzenia.
 * 
 * @note Bufor `data` musi być odpowiednio zainicjalizowany przed wywołaniem tej funkcji.
 */
esp_err_t i2c_read_register(uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t len);

#endif // I2C_DRIVER_H
