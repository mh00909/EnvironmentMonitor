#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "esp_wifi.h"
#include <sys/socket.h>
#include <netdb.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_netif.h"
#include "esp_task_wdt.h"
#include "esp_http_client.h"
#include "wifi_station.h"
#include "light_sensor.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "driver/i2c.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "wifi_ap.h"
#include "mqtt_publisher.h"
#include "bmp280.h"
#include "http_server.h"
#include "i2c_driver.h"
#include "esp_log.h"
#include <esp_timer.h>
#include "ble_sensor.h"
#include "esp_bt.h"
#include "esp_sleep.h"


#define BLINK_GPIO 2
#define BUTTON_GPIO_PIN 0
#define READ_GPIO GPIO_NUM_13


static bool led_state = false;  // stan diody ON/OFF
bool is_config_mode = false;
extern httpd_handle_t server;
bool is_measuring = false;

static esp_timer_handle_t button_timer; // Timer do obsługi timeout dla kliknięć
static uint32_t click_count = 0;        // Licznik kliknięć
static const uint32_t double_click_timeout_ms = 500; // 500 ms na podwójne kliknięcie

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

TaskHandle_t config_task_handle = NULL;


/* Przyciski */

// Inicjalizacja przycisków
void gpio_init(void) {
    // Konfiguracja GPIO dla LED
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED1_GPIO) | (1ULL << LED2_GPIO), // Piny LED
        .mode = GPIO_MODE_OUTPUT, // Wyjście
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE // Brak przerwań
    };
    gpio_config(&io_conf);
}

// Obsługa przerwania dla przycisku 
static void IRAM_ATTR button_isr_handler(void* arg) {
    static uint32_t last_interrupt_time = 0;
    uint32_t current_interrupt_time = xTaskGetTickCountFromISR();

    if ((current_interrupt_time - last_interrupt_time) > pdMS_TO_TICKS(200)) { // przynajmniej 200 ms
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        portENTER_CRITICAL_ISR(&mux); // Synchronizacja
        click_count++;
        portEXIT_CRITICAL_ISR(&mux);

        esp_timer_stop(button_timer);
        esp_timer_start_once(button_timer, double_click_timeout_ms * 1000); 
    }

    last_interrupt_time = current_interrupt_time;
}


void IRAM_ATTR read_gpio_isr_handler(void* arg) {
    static uint32_t last_interrupt_time = 0;
    uint32_t current_interrupt_time = xTaskGetTickCountFromISR();

    // Sprawdź, czy przerwanie jest wyzwalane rzadziej niż co 200 ms
    if ((current_interrupt_time - last_interrupt_time) > pdMS_TO_TICKS(200)) {
    //    ESP_LOGI("ISR", "Przerwanie wywołane na READ_GPIO.");

        if (!is_measuring) {
            is_measuring = true;
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            vTaskNotifyGiveFromISR(config_task_handle, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR();
        }
    }

    last_interrupt_time = current_interrupt_time;
}



// Wykrywanie kliknięć przycisku (pojedyncze lub podwójne) 
void button_timer_callback(void* arg) {
    uint32_t clicks;

    portENTER_CRITICAL_ISR(&mux); // Synchronizacja 
    clicks = click_count;
    click_count = 0; // Zresetuj licznik
    portEXIT_CRITICAL_ISR(&mux);

    if (clicks == 1) {
        // Obsługa pojedynczego kliknięcia
        if(!is_config_mode) {
            esp_wifi_start();
            connect_to_wifi();
            bmp280_set_mode(BMP280_FORCED_MODE);
            while (bmp280_is_measuring()) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            bmp280_read_data(&current_temperature_bmp280, &current_pressure_bmp280);
            current_pressure_bmp280 /= 100.0; // Konwersja ciśnienia na hPa
            ESP_LOGI("BMP280", "Temperatura: %.2f °C, Ciśnienie: %.2f hPa", current_temperature_bmp280, current_pressure_bmp280);

            // Wykonanie pomiaru światła
            light_sensor_read(&current_light);
            ESP_LOGI("LIGHT", "Światło: %d lux", current_light);

            // Publikacja MQTT
            for (int i = 0; i < user_count; i++) {
                for (int j = 0; j < users[i].device_count; j++) {
                    // Wykonaj pomiary i publikuj dane
                    publish_bmp280_data(users[i].user_id, users[i].devices[j].device_id);
                    publish_light_sensor_data(users[i].user_id, users[i].devices[j].device_id);
                    publish_ble_data(users[i].user_id, users[i].devices[j].device_id);
                }
            }
        } else { // wyjście z trybu konfiguracji
            xTaskNotify(config_task_handle, 2, eSetValueWithoutOverwrite);
        }

    } else if (clicks == 2) {
        // Obsługa podwójnego kliknięcia
        if (!is_config_mode) {
            is_config_mode = true;
            mqtt_stop();
            if (server != NULL) stop_webserver(&server);

            ESP_LOGI("CONFIG", "Przełączanie do trybu konfiguracji...");
            gpio_set_level(BLINK_GPIO, 1);
            esp_wifi_stop();
            vTaskDelay(pdMS_TO_TICKS(500));
            wifi_init_ap();
            server = start_webserver();
        }
    }
}

// Konfiguracja przycisku 
void configure_button() {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, // Zmiana z wysokiego na niski
        .mode = GPIO_MODE_INPUT,        // Pin jako wejście
        .pin_bit_mask = (1ULL << BUTTON_GPIO_PIN), // Przesunięcie bitu odpowiadającego pinowi przycisku
        .pull_down_en = 0,
        .pull_up_en = 1,        
    };
    gpio_config(&io_conf);

    gpio_isr_handler_add(BUTTON_GPIO_PIN, button_isr_handler, NULL);

    // Inicjalizacja timera
    const esp_timer_create_args_t timer_args = {
        .callback = &button_timer_callback,
        .name = "button_timer"
    };
    esp_timer_create(&timer_args, &button_timer);
}

/* Nasłuchuje na powiadomienia z systemu i przełącza między trybem AP i STA */
void config_mode_task(void* arg) {
    uint32_t notify_value;
    while (1) {
        // Oczekiwanie na powiadomienie
        if (xTaskNotifyWait(0, ULONG_MAX, &notify_value, portMAX_DELAY) == pdTRUE) {
            if (notify_value == 1) { // Przejście do trybu konfiguracji
                if (!is_config_mode) {
                    is_config_mode = true;
                    ESP_LOGI("CONFIG", "Przełączanie do trybu konfiguracji...");
                    mqtt_stop();
                    if (server != NULL) {
                        stop_webserver(&server);
                    }
                    
                    gpio_set_level(BLINK_GPIO, 1);
                    esp_wifi_stop();
                    vTaskDelay(pdMS_TO_TICKS(500));
                    
                    wifi_init_ap();
                    server = start_webserver();
                    if (server == NULL) {
                        ESP_LOGE("CONFIG", "Nie udało się uruchomić serwera HTTP.");
                    } else {
                        ESP_LOGI("CONFIG", "Tryb AP uruchomiony.");
                    }
    
                }
            } else if (notify_value == 2) { // Przejście do trybu station

                if (is_config_mode) {
                    is_config_mode = false;
                    
                    ESP_LOGI("CONFIG", "MQTT zatrzymane przed przejściem do STA.");
                    mqtt_stop();
                    
                    if (server != NULL) {
                        stop_webserver(&server);
                    }
                    gpio_set_level(BLINK_GPIO, 0);
                    esp_wifi_stop();
                    vTaskDelay(pdMS_TO_TICKS(500));

                    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
                    if (ap_netif) {
                        esp_netif_destroy(ap_netif);
                    }

                    wifi_init_sta();
                    vTaskDelay(pdMS_TO_TICKS(200));
                    connect_to_wifi();
                    vTaskDelay(pdMS_TO_TICKS(200));
                    mqtt_initialize();
                }
            }
        }
    }
}

void configure_read_gpio() {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, // Przerwanie przy opadającym zboczu
        .mode = GPIO_MODE_INPUT,        // Pin jako wejście
        .pin_bit_mask = (1ULL << READ_GPIO), // Przypisanie GPIO_NUM_13
        .pull_down_en = 0,
        .pull_up_en = 1,                // Włącz pull-up dla przycisku
    };
    gpio_config(&io_conf);

    gpio_isr_handler_add(READ_GPIO, read_gpio_isr_handler, NULL);
}



void read_gpio_task(void* arg) {
    while (1) {
        // Czekaj na powiadomienie
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGI("READ", "Rozpoczynanie pomiaru czujników...");

        // Wykonaj pomiar temperatury i ciśnienia z BMP280
        float temperature, pressure;
        bmp280_read_data(&temperature, &pressure);
        pressure /= 100.0; // Konwersja ciśnienia na hPa
        ESP_LOGI("BMP280", "Temperatura: %.2f °C, Ciśnienie: %.2f hPa", temperature, pressure);

        // Wykonaj pomiar światła
        int light_level = 0;
        light_sensor_read(&light_level);
        ESP_LOGI("LIGHT", "Poziom światła: %d lux", light_level);

        // Odczyt danych BLE, jeśli są dostępne
        if (ble_connected) {
            esp_err_t ble_status = read_ble_data();
            if (ble_status == ESP_OK) {
                ESP_LOGI("BLE", "Temperatura BLE: %.2f °C, Wilgotność BLE: %.2f%%", 
                         current_temperature_ble, current_humidity_ble);
            } else {
                ESP_LOGW("BLE", "Nie udało się odczytać danych BLE.");
            }
        }

        // Zakończenie pomiaru
        is_measuring = false;
        ESP_LOGI("READ", "Pomiary zakończone.");
    }
}




/* Diody */

// Konfigurowanie pinu do diody 
static void configure_led(void)
{
    gpio_reset_pin(BLINK_GPIO);  // Reset pinu
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);  // Ustawienie pinu jako wyjście  
}

// Włączenie diody 
void led_on(int led_gpio) {
    gpio_set_level(led_gpio, 1);
}

// Wyłączenie diody
void led_off(int led_gpio) {
    gpio_set_level(led_gpio, 0);
}

// Task migania diodą 
void led_blink_task(void *pvParameter)
{
    while (1) {
        if (is_config_mode) {
            gpio_set_level(BLINK_GPIO, 1); // stałe światło
        } else if (!wifi_connected) {
            gpio_set_level(BLINK_GPIO, led_state);
            led_state = !led_state;
            vTaskDelay(500 / portTICK_PERIOD_MS);  // Miga co 0.5 s
        } else {
            gpio_set_level(BLINK_GPIO, 0); // Wyłączona dioda gdy jest połączenie
            vTaskDelay(1000 / portTICK_PERIOD_MS);  // Sprawdzaj co 1 s
        }
     //   esp_task_wdt_reset();
    }
}

// Monitorowanie temperatury i światła, sprawdzenie czy włączyć diody
void monitor_conditions_task(void *pvParameters) {
    while (1) {
        // Sprawdź temperaturę
        if (current_temperature_bmp280 < min_temperature_threshold || current_temperature_bmp280 > max_temperature_threshold) {
           // ESP_LOGI("MONITOR", "Temperature out of range: %.2f°C", current_temperature_bmp280);
            led_on(LED1_GPIO);
        } else {
            led_off(LED1_GPIO);
        }

        // Sprawdź światło
        if (current_light < min_light_threshold || current_light > max_light_threshold) {
           // ESP_LOGI("MONITOR", "Light out of range: %d lux", current_light);
            led_on(LED2_GPIO);
        } else {
            led_off(LED2_GPIO);
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); 
    }
}


/* Domyślna konfiguracja BMP280 */
bmp280_config_t bmp280_default_config = {
    .oversampling_temp = BMP280_OSRS_X1, 
    .oversampling_press = BMP280_OSRS_X1,
    .standby_time = 0x05,
    .filter = 0x04,
    .mode = BMP280_SLEEP_MODE
};

/* Zapis domyślnej konfiguracji MQTT */
void save_default_mqtt_config() {
    const char* default_broker = "mqtt://192.168.57.30";
    int default_port = 1883;
    const char* default_user = "username";
    const char* default_password = "password";

    save_mqtt_config_to_nvs(default_broker, default_port, default_user, default_password);
}

/* Inicjalizacja BLE */
esp_err_t ble_initialize() {
    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret) {
        ESP_LOGE("BLE", "Bluetooth controller release classic BT memory failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE("BLE", "Bluetooth controller initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE("BLE", "Bluetooth controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE("BLE", "Bluedroid initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE("BLE", "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Rejestracja GAP i GATTC callbacków
    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret) {
        ESP_LOGE("BLE", "%s gap register failed, error code = %x", __func__, ret);
     
    }
    ret = esp_ble_gattc_register_callback(esp_gattc_cb);
    if (ret) {
        ESP_LOGE("BLE", "%s gattc register failed, error code = %x", __func__, ret);
  
    }
    ret = esp_ble_gattc_app_register(0);
    if (ret) {
        ESP_LOGE("BLE", "%s gattc app register failed, error code = %x", __func__, ret);
    }

    ESP_LOGI("BLE", "BLE initialized successfully.");
    return ESP_OK;
}



void app_main(void) {
    esp_err_t ret;

    // Inicjalizacja NVS
    ESP_LOGI("MAIN", "Rozpoczynam inicjalizację NVS...");
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW("MAIN", "NVS nieprawidłowe. Wymazywanie...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_LOGI("MAIN", "NVS zainicjalizowane pomyślnie.");

    // Inicjalizacja TCP/IP i systemu zdarzeń
    ESP_LOGI("MAIN", "Inicjalizacja stosu TCP/IP i systemu zdarzeń...");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Inicjalizacja GPIO, przycisków i LED
    ESP_LOGI("MAIN", "Inicjalizacja GPIO i przycisków...");
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3));
    gpio_init();
    configure_button();
    configure_read_gpio();
    configure_led();



    // Inicjalizacja Wi-Fi
    ESP_LOGI("MAIN", "Inicjalizacja Wi-Fi...");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_init_sta();

    // Rozpoczęcie połączenia Wi-Fi
    ESP_LOGI("MAIN", "Łączenie z siecią Wi-Fi...");
    connect_to_wifi();

    // Inicjalizacja magistrali I2C i czujników
    ESP_LOGI("MAIN", "Inicjalizacja magistrali I2C i czujników...");
    i2c_master_init();
    vTaskDelay(pdMS_TO_TICKS(100));
    bmp280_init();
    load_bmp280_config_from_nvs(&bmp280_default_config);
    bmp280_apply_config(&bmp280_default_config);
    light_sensor_init();

    // Utworzenie taska do zarządzania trybami pracy
    ESP_LOGI("MAIN", "Tworzenie taska do zarządzania trybami pracy...");
    xTaskCreate(config_mode_task, "config_mode_task", 4096, NULL, 4, &config_task_handle);

    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI("MAIN", "Tworzenie taska do obsługi pomiarów...");
    xTaskCreate(read_gpio_task, "read_gpio_task", 4096, NULL, 5, &config_task_handle);


    // Uruchomienie serwera HTTP
    ESP_LOGI("MAIN", "Uruchamianie serwera HTTP...");
    server = start_webserver();
    if (server == NULL) {
        ESP_LOGE("MAIN", "Nie udało się uruchomić serwera HTTP.");
    } else {
        ESP_LOGI("MAIN", "Serwer HTTP uruchomiony pomyślnie.");
    }

    

    vTaskDelay(pdMS_TO_TICKS(1000));


///
    ESP_LOGI("MEMORY", "Wolna pamięć heap: %ld bytes", esp_get_free_heap_size());
    UBaseType_t uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI("MEMORY", "Minimalna pamięć stosu dla taska: %d bytes", uxHighWaterMark);
///

    // Sprawdzenie połączenia Wi-Fi i inicjalizacja MQTT
    if (wifi_connected) {
        ESP_LOGI("MAIN", "Inicjalizacja MQTT...");
        initialize_mqtt_mutex();

        ESP_LOGI("MEMORY", "Wolna pamięć heap: %ld bytes", esp_get_free_heap_size());
        UBaseType_t uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGI("MEMORY", "Minimalna pamięć stosu dla taska: %d bytes", uxHighWaterMark);

        vTaskDelay(pdMS_TO_TICKS(500));
        save_default_mqtt_config();
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_LOGI("MEMORY", "Wolna pamięć heap: %ld bytes", esp_get_free_heap_size());
        uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGI("MEMORY", "Minimalna pamięć stosu dla taska: %d bytes", uxHighWaterMark);

        mqtt_initialize();

        ESP_LOGI("MEMORY", "Wolna pamięć heap: %ld bytes", esp_get_free_heap_size());
        uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGI("MEMORY", "Minimalna pamięć stosu dla taska: %d bytes", uxHighWaterMark);

    } else {
        ESP_LOGE("MAIN", "Wi-Fi nie jest połączone. Inicjalizacja MQTT pominięta.");
    }

    // Utworzenie taska monitorowania warunków
    ESP_LOGI("MAIN", "Tworzenie taska monitorującego warunki środowiskowe...");
    xTaskCreate(&monitor_conditions_task, "monitor_conditions_task", 8192, NULL, 3, NULL);


    ESP_LOGI("MAIN", "Inicjalizacja zakończona pomyślnie.");


    // Inicjalizacja BLE
    ESP_LOGI("MAIN", "Inicjalizacja BLE...");
    ret = ble_initialize();
    if (ret != ESP_OK) {
        ESP_LOGE("MAIN", "Błąd inicjalizacji BLE: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI("MAIN", "BLE zainicjalizowane pomyślnie.");
    }

    // Rozpoczęcie skanowania BLE
    if (esp_ble_gap_is_scanning()) {
        ESP_LOGW("BLE", "Skanowanie BLE już aktywne. Zatrzymuję...");
        esp_ble_gap_stop_scanning();
    }
    esp_err_t scan_ret = esp_ble_gap_start_scanning(60);
    if (scan_ret != ESP_OK) {
        ESP_LOGE("BLE", "Nie udało się rozpocząć skanowania BLE: %s", esp_err_to_name(scan_ret));
    } else {
        ESP_LOGI("BLE", "Skanowanie BLE rozpoczęte.");
    }
}
