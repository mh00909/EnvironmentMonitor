#include "bmp280_sensor.h"
#include "driver/i2c.h"
#include "bmp280.h"
#include "esp_log.h"


static const char *TAG = "BMP280_SENSOR";

static bmp280_t bmp280_dev;
static bmp280_params_t bmp280_params;



#define I2C_MASTER_SCL_IO 22        // GPIO SCL
#define I2C_MASTER_SDA_IO 21        // GPIO SDA
#define I2C_MASTER_FREQ_HZ 100000   // Prędkość I2C (100 kHz)
#define I2C_MASTER_NUM I2C_NUM_0    // Port I2C
#define I2C_MASTER_TIMEOUT_MS 1000  // Timeout


void i2c_master_init() {
    static bool i2c_initialized = false; // Flaga inicjalizacji

    if (i2c_initialized) {
        ESP_LOGW("I2C", "I2C driver already initialized");
        return;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, I2C_MODE_MASTER, 0, 0, 0));

    i2c_initialized = true; // Ustaw flagę
}


esp_err_t bmp280_init_sensor(void) {
    bmp280_t bmp280;
    bmp280_params_t params;
    bmp280_init_default_params(&params);

    // Inicjalizacja deskryptora BMP280
    ESP_ERROR_CHECK(bmp280_init_desc(&bmp280, BMP280_I2C_ADDRESS_0, I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO));

    // Inicjalizacja czujnika BMP280
    ESP_ERROR_CHECK(bmp280_init(&bmp280, &params));

    while (1) {
        float temperature, pressure;
        if (bmp280_read_float(&bmp280, &temperature, &pressure, NULL) == ESP_OK) {
            printf("Temp: %.2f °C, Pressure: %.2f hPa\n", temperature, pressure);
        } else {
            printf("Failed to read from BMP280\n");
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // Odczyt co 1 sekundę
    }

    return ESP_OK;
}
