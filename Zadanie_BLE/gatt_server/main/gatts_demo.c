#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h> 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "driver/gpio.h" 

#define TAG "BLE_SERVER"

// UUIDy usług i charakterystyk
#define UUID_ENV_SERVICE    0x181A
#define UUID_TEMP_CHAR      0x2A1F
#define UUID_HUMID_CHAR     0x2A6F
#define UUID_BATTERY_SERVICE 0x180F
#define UUID_BATTERY_CHAR    0x2A19

// Liczba uchwytów dla każdej usługi
#define HANDLE_NUM_ENV      7   // deklaracja usługi, charakterystyki temperatury(dekl.+wartość+deskryptor), charakterystyki wilgotności(dekl.+wartość+deskryptor)
#define HANDLE_NUM_BATTERY  4   // deklaracja usługi, charakterystyki baterii(dekl.+wartość+deskryptor)


#define DEVICE_NAME "ESP32_"

#define LED_GPIO_PIN GPIO_NUM_2

static bool delay_applied = false;

// Struktura do przechowywania informacji o profilu BLE serwera GATT
struct gatts_profile {
    uint16_t service_handle; // Uchwyt dla danej usługi

    // Uchwyty dla charakterystyk
    uint16_t temp_char_handle;  
    uint16_t humid_char_handle; 
    uint16_t battery_char_handle; 
    
    uint16_t conn_id; // identyfikator sesji BLE

    // Deskryptory CCCD do charakterystyk obsługujących powiadomienia
    uint16_t temp_char_cccd_handle; 
    uint16_t humid_char_cccd_handle; 
    uint16_t battery_char_cccd_handle;
    
    // Czy powiadomienia dla charakterystyk są włączone
    bool temp_notify_enabled; 
    bool humid_notify_enabled; 
    bool battery_notify_enabled;      

};

// Instancje dla usług
static struct gatts_profile env_profile = {0};
static struct gatts_profile battery_profile = {0};


static float temperature = 25.5;
static float humidity = 65.0;
static uint8_t battery_level = 85;


void notify_task(void *arg) {
    esp_gatt_if_t gatts_if = (esp_gatt_if_t)arg;
    ESP_LOGI(TAG, "Notification task started");

    while (1) {
     
        if (env_profile.conn_id == 0xFFFF) {
            ESP_LOGW(TAG, "Connection not active, skipping notifications");
            env_profile.temp_notify_enabled = false;
            env_profile.humid_notify_enabled = false;
            env_profile.battery_notify_enabled = false;
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        
        if (!delay_applied) {
            ESP_LOGI(TAG, "Waiting for client to initialize...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            delay_applied = true;
        }

        // Losowanie danych
        temperature = rand() % (35 + 20 + 1) / 1.0 - 20; // Zakres -20°C do 35°C
        humidity = rand() % (97 - 30 + 1) / 1.0 + 30;    // Zakres 30% do 97%
        battery_level = rand() % (100 - 10 + 1) / 1.0 + 10.0; // Zakres 10% do 100%

        int16_t temp = (int16_t)(temperature * 10); 
        int16_t hum = (int16_t)(humidity * 100);  

        // Wysyłanie powiadomień
        if (env_profile.temp_notify_enabled) {
            ESP_LOGI(TAG, "Sending temperature notification: %.1f°C", temperature);
            esp_err_t ret = esp_ble_gatts_send_indicate(
                gatts_if,
                env_profile.conn_id,
                env_profile.temp_char_handle,
                sizeof(temp),
                (uint8_t *)&temp,
                false);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send temperature notification: %s", esp_err_to_name(ret));
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        if (env_profile.humid_notify_enabled) {
            ESP_LOGI(TAG, "Sending humidity notification: %.1f%%", humidity);
            esp_err_t ret = esp_ble_gatts_send_indicate(
                gatts_if,
                env_profile.conn_id,
                env_profile.humid_char_handle,
                sizeof(hum),
                (uint8_t *)&hum,
                false);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send humidity notification: %s", esp_err_to_name(ret));
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        if (battery_profile.battery_notify_enabled) {
            ESP_LOGI(TAG, "Sending battery level notification: %d%%", battery_level);
            uint8_t battery = battery_level;
            esp_err_t ret = esp_ble_gatts_send_indicate(
                gatts_if,
                battery_profile.conn_id,
                battery_profile.battery_char_handle,
                sizeof(battery),
                &battery,
                false);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send battery notification: %s", esp_err_to_name(ret));
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        vTaskDelay(pdMS_TO_TICKS(5000)); // Co 5 sekund
    }
}
// Reklamowanie
static void setup_ble_adv() {

    ESP_ERROR_CHECK(esp_ble_gap_set_device_name(DEVICE_NAME));

    // definiuje dane reklamowe
    esp_ble_adv_data_t adv_data = {
        .set_scan_rsp = false, // wysyłane w pakiecie reklamowym, a nie w odpowiedzi skanowania
        .include_name = true,  // Dołącz nazwę urządzenia
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT), // Urządzenie w trybie ogólnego wykrywania, tylko BLE
    };

    esp_ble_adv_params_t adv_params = {
        .adv_int_min = 0x20, // Min interwał reklamowania
        .adv_int_max = 0x40, // Max interwał reklamowania
        .adv_type = ADV_TYPE_IND, // Ogólne reklamowanie
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .channel_map = ADV_CHNL_ALL, 
        .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY, // Skany i połączenia od dowolnych urządzeń
    };

    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&adv_data)); // Konfiguracja danych reklamowych
    ESP_ERROR_CHECK(esp_ble_gap_start_advertising(&adv_params)); // Rozpoczęcie reklamowania
    ESP_LOGI(TAG, "Advertising started");
}

static bool conn_update_pending = false; // czy jest teraz aktualizacja parametrów połączenia
// Funkcja obsługi zdarzeń GATT
// Parametry: typ zdarzenia BLE, identyfikator interfejsu BLE, struktura zawierająca dane bieżącego zdarzenia
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    esp_gatt_rsp_t rsp; // odpowiedź na żądanie odczytu

    switch (event) {

        // Dodano charakterystykę
        case ESP_GATTS_ADD_CHAR_EVT:
            if (param->add_char.char_uuid.uuid.uuid16 == UUID_TEMP_CHAR) { 
                env_profile.temp_char_handle = param->add_char.attr_handle; // Przypisanie uchwytu
            } else if (param->add_char.char_uuid.uuid.uuid16 == UUID_HUMID_CHAR) {
                env_profile.humid_char_handle = param->add_char.attr_handle;
            } else if (param->add_char.char_uuid.uuid.uuid16 == UUID_BATTERY_CHAR) {
                battery_profile.battery_char_handle = param->add_char.attr_handle;
            }
            break;

        // Desktyptor charakterystyki został pomyślnie dodany do BLE serwera
        case ESP_GATTS_ADD_CHAR_DESCR_EVT:
            if (param->add_char_descr.attr_handle == env_profile.temp_char_handle + 1) {// uchwyt dekryptora = uchwyt charakterystyki + 1
                env_profile.temp_char_cccd_handle = param->add_char_descr.attr_handle;
            } else if (param->add_char_descr.attr_handle == env_profile.humid_char_handle + 1) {
                env_profile.humid_char_cccd_handle = param->add_char_descr.attr_handle;
            }
            if (param->add_char_descr.attr_handle == battery_profile.battery_char_handle + 1) {
                battery_profile.battery_char_cccd_handle = param->add_char_descr.attr_handle;
            }
            break;

        // Rejestracja profilu GATT została zakończona, funkcja jest gotowa do utworzenia usług i charakterystyk dla danego profilu BLE
        case ESP_GATTS_REG_EVT:
            if (param->reg.app_id == 0) {  // Environmental Service
                esp_ble_gatts_create_service(gatts_if, &(esp_gatt_srvc_id_t){ // tworzenie nowej usługi BLE
                    .is_primary = true,  // usługa podstawowa
                    .id = {.uuid = {.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = UUID_ENV_SERVICE}}}}, HANDLE_NUM_ENV);
                
            } else if (param->reg.app_id == 1) {  // Battery Service
                esp_ble_gatts_create_service(gatts_if, &(esp_gatt_srvc_id_t){
                    .is_primary = true, 
                    .id = {.uuid = {.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = UUID_BATTERY_SERVICE}}}}, HANDLE_NUM_BATTERY);
            }
            break;

        // usługa BLE została utworzona, serwer BLE jest gotowy do dodania do niej charakterystyk i deskryptorów
        case ESP_GATTS_CREATE_EVT:
            if (param->create.service_id.id.uuid.uuid.uuid16 == UUID_ENV_SERVICE) { // Environment Service
                
                env_profile.service_handle = param->create.service_handle; // uchwyt usługi środowiskowej

                // Dodanie charakterystyki temperatury
                esp_ble_gatts_add_char(env_profile.service_handle, &(esp_bt_uuid_t){
                    .len = ESP_UUID_LEN_16, .uuid = {.uuid16 = UUID_TEMP_CHAR}},
                    ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, // Odczyt i zapis
                    ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY, 
                    NULL, NULL); // parametry początkowe

                // Dodanie deskryptora CCCD dla temperatury
                esp_ble_gatts_add_char_descr(env_profile.service_handle, &(esp_bt_uuid_t){
                    .len = ESP_UUID_LEN_16, .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG}},
                    ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, // Odczyt i zapis
                    NULL, NULL);

                // Dodanie charakterystyki wilgotności
                esp_ble_gatts_add_char(env_profile.service_handle, &(esp_bt_uuid_t){
                    .len = ESP_UUID_LEN_16, .uuid = {.uuid16 = UUID_HUMID_CHAR}},
                    ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                    ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                    NULL, NULL);

                // Dodanie deskryptora CCCD dla wilgotności
                esp_ble_gatts_add_char_descr(env_profile.service_handle, &(esp_bt_uuid_t){
                    .len = ESP_UUID_LEN_16, .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG}},
                    ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                    NULL, NULL);

                // Rozpoczęcie usługi
                esp_ble_gatts_start_service(env_profile.service_handle);

                // Uruchomienia taska do wysyłania powiadomień
                xTaskCreate(notify_task, "notify_task", 2048, (void *)gatts_if, 5, NULL);
            }
            if (param->create.service_id.id.uuid.uuid.uuid16 == UUID_BATTERY_SERVICE) { // Battery Service
                
                battery_profile.service_handle = param->create.service_handle; // uchwyt serwisu baterii

                // Dodanie charakterystyki poziomu baterii
                esp_ble_gatts_add_char(
                    battery_profile.service_handle,
                    &(esp_bt_uuid_t){
                        .len = ESP_UUID_LEN_16,
                        .uuid = {.uuid16 = UUID_BATTERY_CHAR}
                    },
                    ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 
                    ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY, 
                    NULL, NULL
                );

                // Dodanie deskryptora CCCD dla baterii
                esp_ble_gatts_add_char_descr(
                    battery_profile.service_handle,
                    &(esp_bt_uuid_t){
                        .len = ESP_UUID_LEN_16,
                        .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG}
                    },
                    ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 
                    NULL, NULL
                );

                // Rozpoczęcie usługi
                esp_ble_gatts_start_service(battery_profile.service_handle);
            }
            break;

        // Odczyt charakterystyk
        case ESP_GATTS_READ_EVT:
            ESP_LOGI(TAG, "ESP_GATTS_READ_EVT, handle = %d", param->read.handle);
            memset(&rsp, 0, sizeof(esp_gatt_rsp_t)); //  zeruje wszystkie pola struktury
            rsp.attr_value.handle = param->read.handle; // ustawienie pola handle na uchwyt danej charakterystyki

            if (param->read.handle == env_profile.temp_char_handle) {

                int16_t temp = (int16_t)(temperature * 10); 
                rsp.attr_value.value[0] = temp & 0xFF;    
                rsp.attr_value.value[1] = (temp >> 8) & 0xFF;
                rsp.attr_value.len = 2;            
            } else if (param->read.handle == env_profile.humid_char_handle) {
    
                int16_t hum = (int16_t)(humidity * 100);  
                rsp.attr_value.value[0] = hum & 0xFF;   
                rsp.attr_value.value[1] = (hum >> 8) & 0xFF; 
                rsp.attr_value.len = 2;                    
            } else if (param->read.handle == battery_profile.battery_char_handle) {
     
                rsp.attr_value.value[0] = battery_level;   // Jednobajtowa wartość poziomu baterii
                rsp.attr_value.len = 1;                   
            } else {
                rsp.attr_value.len = 0; // Domyślnie brak danych
            }

            // wysłanie odpowiedzi
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
            break;

        
        // Klient wysyła dane 
        case ESP_GATTS_WRITE_EVT: {
            ESP_LOGI(TAG, "ESP_GATTS_WRITE_EVT, handle = %d", param->write.handle);

   
            if (param->write.handle == env_profile.temp_char_handle) {
                // Pobranie wartości zapisanej przez klienta
                if (param->write.len == sizeof(int16_t)) {
                    int16_t received_temp = (param->write.value[1] << 8) | param->write.value[0];
                    float temperature_value = received_temp / 10.0;
                    ESP_LOGI(TAG, "Received temperature value: %.1f°C", temperature_value);
                } else {
                    ESP_LOGW(TAG, "Unexpected write length for temperature characteristic.");
                }

                // Zapalenie diody na 2 sekundy
                gpio_set_level(LED_GPIO_PIN, 1);  
                vTaskDelay(pdMS_TO_TICKS(2000));  
                gpio_set_level(LED_GPIO_PIN, 0); 
            }

            if (param->write.handle == env_profile.humid_char_handle) {

                if (param->write.len == sizeof(int16_t)) {
                    int16_t received_hum = (param->write.value[1] << 8) | param->write.value[0];
                    float humidity_value = received_hum / 100.0;
                    ESP_LOGI(TAG, "Received humidity value: %.1f%%", humidity_value);
                } else {
                    ESP_LOGW(TAG, "Unexpected write length for humidity characteristic.");
                }

                gpio_set_level(LED_GPIO_PIN, 1);  
                vTaskDelay(pdMS_TO_TICKS(2000));
                gpio_set_level(LED_GPIO_PIN, 0); 
            }

            if (param->write.handle == env_profile.temp_char_cccd_handle) {
                uint16_t config = param->write.value[0] | (param->write.value[1] << 8);
                env_profile.temp_notify_enabled = (config == 0x0001);
                ESP_LOGI(TAG, "Temperature notifications %s", env_profile.temp_notify_enabled ? "enabled" : "disabled");
            }

            if (param->write.handle == env_profile.humid_char_cccd_handle) {
                uint16_t config = param->write.value[0] | (param->write.value[1] << 8);
                env_profile.humid_notify_enabled = (config == 0x0001);
                ESP_LOGI(TAG, "Humidity notifications %s", env_profile.humid_notify_enabled ? "enabled" : "disabled");
            }

            if (!param->write.is_prep) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
            }
            break;
        }


        
        case ESP_GATTS_CONNECT_EVT: {
            ESP_LOGI(TAG, "Client connected, conn_id = %d", param->connect.conn_id);
            env_profile.conn_id = param->connect.conn_id;
    
            if (!conn_update_pending) {
                esp_ble_conn_update_params_t conn_params = {
                    .latency = 0,
                    .max_int = 0x50,  
                    .min_int = 0x30, 
                    .timeout = 1000 
                };
                memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
                esp_err_t ret = esp_ble_gap_update_conn_params(&conn_params);
                if (ret == ESP_OK) {
                    conn_update_pending = true;
                } else {
                    ESP_LOGW(TAG, "Connection update failed: %s", esp_err_to_name(ret));
                }
            }
            break;
        }
        case ESP_GATTS_DISCONNECT_EVT: {
            ESP_LOGI(TAG, "Client disconnected, reason = %d", param->disconnect.reason);
            env_profile.conn_id = 0xFFFF;
            delay_applied = false; 
            conn_update_pending = false; 
            env_profile.temp_notify_enabled = false;
            env_profile.humid_notify_enabled = false;
            env_profile.battery_notify_enabled = false;
            setup_ble_adv();
            break;
        }


        case ESP_GATTS_MTU_EVT: {
            uint16_t mtu_size = param->mtu.mtu < 247 ? param->mtu.mtu : 247;
            break;
        }



        default:
            break;
        }
}


void app_main(void) {
    esp_err_t ret;
    srand(time(NULL));  

    gpio_reset_pin(LED_GPIO_PIN);
    gpio_set_direction(LED_GPIO_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO_PIN, 0);  // Wyłącz LED

    // Inicjalizacja NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Inicjalizacja Bluetooth
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // Rejestracja callbacków
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gap_set_device_name(DEVICE_NAME));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(0));  // Environmental Service
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(1));  // Battery Service
   

    // Reklamowanie
    setup_ble_adv();
}