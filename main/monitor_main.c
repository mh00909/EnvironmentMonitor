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
#include "power_manager.h"
#include "esp_bt.h"

#define BLINK_GPIO 2
#define BUTTON_GPIO_PIN 0



#define BUTTON_GPIO GPIO_NUM_18


static bool led_state = false;  // stan diody ON/OFF
bool is_config_mode = false;
extern httpd_handle_t server;

static esp_timer_handle_t button_timer; // Timer do obsługi timeout dla kliknięć
static uint32_t click_count = 0;        // Licznik kliknięć
static const uint32_t double_click_timeout_ms = 500; // 500 ms na podwójne kliknięcie

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;



void led_on(int led_gpio) {
    gpio_set_level(led_gpio, 1);
}

void led_off(int led_gpio) {
    gpio_set_level(led_gpio, 0);
}


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

    // Konfiguracja GPIO dla przycisku
    io_conf.pin_bit_mask = (1ULL << BUTTON_GPIO); // Pin przycisku
    io_conf.mode = GPIO_MODE_INPUT; // Wejście
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE; // Włącz pull-up (przycisk podłączony do GND)
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);
}

/* Task migania diodą */ 
void led_blink_task(void *pvParameter)
{
    while (1) {
        if (is_config_mode) {
            gpio_set_level(BLINK_GPIO, 1); // stałe światło
            vTaskDelay(500 / portTICK_PERIOD_MS);
        } else if (!wifi_connected) {
            gpio_set_level(BLINK_GPIO, led_state);
            led_state = !led_state;
            vTaskDelay(500 / portTICK_PERIOD_MS);  // Miga co 0.5 s
        } else {
            gpio_set_level(BLINK_GPIO, 0); // Wyłączona dioda gdy jest połączenie
            vTaskDelay(1000 / portTICK_PERIOD_MS);  // Sprawdzaj co 1 s
        }
    }
}

/* Wykrywanie kliknięć przycisku (pojedyncze lub podwójne) */
void button_timer_callback(void* arg) {
    uint32_t clicks;

    portENTER_CRITICAL_ISR(&mux); // Synchronizacja 
    clicks = click_count;
    click_count = 0; // Zresetuj licznik
    portEXIT_CRITICAL_ISR(&mux);

    if (clicks == 1) {
        // Obsługa pojedynczego kliknięcia
        if(!is_config_mode) {

            bmp280_set_mode(BMP280_FORCED_MODE);
            while (bmp280_is_measuring()) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            bmp280_read_data(&current_temperature_bmp280, &current_pressure_bmp280);
            current_pressure_bmp280 = current_pressure_bmp280/100.0;
            ESP_LOGI("BMP280", "Temperatura: %.2f °C, Ciśnienie: %.2f hPa", current_temperature_ble, current_pressure_bmp280);
            light_sensor_read(&current_light);
            int precent = light_sensor_to_percentage(current_light);
            ESP_LOGI("LIGHT", "Light: %d, %d%%", current_light, precent);

            // Publikacja MQTT
            if (!is_config_mode && wifi_connected) {
                publish_data(client_handle,"user1", "device1");

            }
        } else { // wyjście z trybu konfiguracji
            xTaskNotify(config_task_handle, 2, eSetValueWithoutOverwrite);
        }

    } else if (clicks == 2) {
        // Obsługa podwójnego kliknięcia
        if (!is_config_mode) {
            is_config_mode = true;
            mqtt_stop();
            if (server != NULL) stop_webserver(server);

            ESP_LOGI("CONFIG", "Przełączanie do trybu konfiguracji...");
            gpio_set_level(BLINK_GPIO, 1);
            esp_wifi_stop();
            vTaskDelay(pdMS_TO_TICKS(500));
            wifi_init_ap();
            server = start_webserver();
        }
    }
}

/* Nasłuchuje na powiadomienia z systemu i przełącza między trybem AP i STA */
void config_mode_task(void* arg) {
    uint32_t notify_value;
    while (1) {
        // Oczekiwanie na powiadomienie
        if (xTaskNotifyWait(0, ULONG_MAX, &notify_value, portMAX_DELAY) == pdTRUE) {
            //ESP_LOGI("CONFIG", "Otrzymano powiadomienie: %u", notify_value);

            if (notify_value == 1) { // Przejście do trybu konfiguracji
                if (!is_config_mode) {
                    is_config_mode = true;
                    mqtt_stop();
                    if (server != NULL) {
                        stop_webserver(server);
                    }
                    ESP_LOGI("CONFIG", "Przełączanie do trybu konfiguracji...");
                    gpio_set_level(BLINK_GPIO, 1);
                    esp_wifi_stop();
                    vTaskDelay(pdMS_TO_TICKS(500));
                    wifi_init_ap();
                    server = start_webserver();
    
                }
            } else if (notify_value == 2) { // Przejście do trybu station

                if (is_config_mode) {
                    is_config_mode = false;
                    mqtt_stop();
                    ESP_LOGI("CONFIG", "MQTT zatrzymane przed przejściem do STA.");
                    ESP_LOGI("CONFIG", "Przełączanie do trybu pracy (Station)...");
                    gpio_set_level(BLINK_GPIO, 0);
                    if (server != NULL) {
                        stop_webserver(server);
                    }
                    esp_wifi_stop();
                    ESP_LOGI("CONFIG", "Wi-Fi zatrzymane.");
                    vTaskDelay(pdMS_TO_TICKS(500));

                    ESP_LOGI("CONFIG", "Niszczenie interfejsu AP...");
                    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
                    if (ap_netif) {
                        esp_netif_destroy(ap_netif);
                        ESP_LOGI("CONFIG", "Interfejs AP zniszczony.");
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

/* Konfigurowanie pinu do diody */
static void configure_led(void)
{
    gpio_reset_pin(BLINK_GPIO);  // Reset pinu
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);  // Ustawienie pinu jako wyjście  
}

TaskHandle_t config_task_handle = NULL;

/* Obsługa przerwania dla przycisku */
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

/* Konfiguracja przycisku */
void configure_button() {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, // Zmiana z wysokiego na niski
        .mode = GPIO_MODE_INPUT,        // Pin jako wejście
        .pin_bit_mask = (1ULL << BUTTON_GPIO_PIN), // Przesunięcie bitu odpowiadającego pinowi przycisku
        .pull_down_en = 0,
        .pull_up_en = 1,        
    };
    gpio_config(&io_conf);

    // Inicjalizacja przerwań GPIO
    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3);
    gpio_isr_handler_add(BUTTON_GPIO_PIN, button_isr_handler, NULL);

    // Inicjalizacja timera
    const esp_timer_create_args_t timer_args = {
        .callback = &button_timer_callback,
        .name = "button_timer"
    };
    esp_timer_create(&timer_args, &button_timer);
}

bmp280_config_t bmp280_default_config = {
    .oversampling_temp = BMP280_OSRS_X1, 
    .oversampling_press = BMP280_OSRS_X1,
    .standby_time = 0x05,
    .filter = 0x04,
    .mode = BMP280_SLEEP_MODE
};

void save_default_mqtt_config() {
    const char* default_broker = "mqtt://192.168.57.30";
    int default_port = 1883;
    const char* default_user = "username";
    const char* default_password = "password";

    save_mqtt_config_to_nvs(default_broker, default_port, default_user, default_password);
}



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


void monitor_conditions_task(void *pvParameters) {
    while (1) {
        // Sprawdź temperaturę
        if (current_temperature_bmp280 < min_temperature_threshold || current_temperature_bmp280 > max_temperature_threshold) {
            led_on(LED1_GPIO);
        } else {
            led_off(LED1_GPIO);
        }

        // Sprawdź światło
        if (current_light < min_light_threshold || current_light > max_light_threshold) {
            led_on(LED2_GPIO);
        } else {
            led_off(LED2_GPIO);
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); 
    }
}

void app_main(void)
{

  //  esp_log_level_set("wifi", ESP_LOG_NONE);

    // Inicjalizacja NVS 
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init()); // Inicjalizacja stosu TCP/IP    
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // Inicjalizacja systemu zdarzeń

    configure_button(); // Konfiguracja przycisku
    configure_led(); // Konfiguracja diody
    
    // Konfiguracja Wi-Fi w trybie STA
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));


    i2c_master_init();
    vTaskDelay(pdMS_TO_TICKS(100)); 

    
    bmp280_init();
    load_bmp280_config_from_nvs(&bmp280_default_config);
    bmp280_apply_config(&bmp280_default_config);

    light_sensor_init();
    

    // Uruchomienie tasków
    xTaskCreate(config_mode_task, "config_mode_task", 4096, NULL, 5, &config_task_handle);
    xTaskCreate(&led_blink_task, "led_blink_task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    

    // Inicjalizacja Wi-Fi i innych modułów
    wifi_init_sta();
    connect_to_wifi();

    // Inicjalizacja BLE
    ret = ble_initialize();
    if (ret != ESP_OK) {
        ESP_LOGE("Main", "Failed to initialize BLE");
        return;
    }
    

    if (esp_ble_gap_stop_scanning() == ESP_OK) {
        ESP_LOGI("BLE", "Scanning stopped successfully.");
    } else {
        ESP_LOGW("BLE", "Failed to stop scanning before starting a new scan.");
    }
    vTaskDelay(pdMS_TO_TICKS(100)); 
    esp_err_t scan_ret = esp_ble_gap_start_scanning(60);
    if (scan_ret != ESP_OK) {
        ESP_LOGE("BLE", "Failed to start scanning: %s", esp_err_to_name(scan_ret));
    } else {
        ESP_LOGI("BLE", "BLE scanning started for 60 seconds.");
    }

    

    vTaskDelay(pdMS_TO_TICKS(500)); 
    server = start_webserver();

    initialize_mqtt_mutex();
    save_default_mqtt_config();

    gpio_init();

    xTaskCreate(&monitor_conditions_task, "monitor_conditions_task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    
    //xTaskCreate(&power_manager_task, "power_manager_task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    set_power_mode(MODE_POWER_SAVE);
}