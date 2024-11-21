#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"

#define TAG "BLE_SERVER"

// UUIDy usług i charakterystyk
#define UUID_ENV_SERVICE    0x181A
#define UUID_TEMP_CHAR      0x2A1F
#define UUID_HUMID_CHAR     0x2A6F
#define UUID_BATTERY_SERVICE 0x180F
#define UUID_BATTERY_CHAR    0x2A19

// Liczba uchwytów dla każdej usługi
#define HANDLE_NUM_ENV      6
#define HANDLE_NUM_BATTERY  4

// Nazwa urządzenia
#define DEVICE_NAME "ESP32_SENSOR"

// Struktura profilu
struct gatts_profile {
    uint16_t service_handle;
    uint16_t temp_char_handle;  // Uchwyt dla temperatury
    uint16_t humid_char_handle; // Uchwyt dla wilgotności
    uint16_t battery_char_handle; // Uchwyt dla baterii
    uint16_t conn_id;
};


// Instancje dla dwóch usług
static struct gatts_profile env_profile = {0};
static struct gatts_profile battery_profile = {0};

// Prosta implementacja danych
static float temperature = 25.5;
static float humidity = 65.0;
static uint8_t battery_level = 85;

// Funkcja obsługi BLE
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    esp_gatt_rsp_t rsp;

    switch (event) {
        case ESP_GATTS_ADD_CHAR_EVT:
            if (param->add_char.char_uuid.uuid.uuid16 == UUID_TEMP_CHAR) {
                env_profile.temp_char_handle = param->add_char.attr_handle;
            } else if (param->add_char.char_uuid.uuid.uuid16 == UUID_HUMID_CHAR) {
                env_profile.humid_char_handle = param->add_char.attr_handle;
            } else if (param->add_char.char_uuid.uuid.uuid16 == UUID_BATTERY_CHAR) {
                battery_profile.battery_char_handle = param->add_char.attr_handle;
            }
            break;

    case ESP_GATTS_REG_EVT:
        if (param->reg.app_id == 0) {  // Environmental Service
            esp_ble_gatts_create_service(gatts_if, &(esp_gatt_srvc_id_t){
                .is_primary = true, 
                .id = {.uuid = {.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = UUID_ENV_SERVICE}}}}, HANDLE_NUM_ENV);
        } else if (param->reg.app_id == 1) {  // Battery Service
            esp_ble_gatts_create_service(gatts_if, &(esp_gatt_srvc_id_t){
                .is_primary = true, 
                .id = {.uuid = {.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = UUID_BATTERY_SERVICE}}}}, HANDLE_NUM_BATTERY);
        }
        break;

    case ESP_GATTS_CREATE_EVT:
        if (param->create.service_id.id.uuid.uuid.uuid16 == UUID_ENV_SERVICE) {
            env_profile.service_handle = param->create.service_handle;
            // Dodaj charakterystyki
            esp_ble_gatts_add_char(env_profile.service_handle, &(esp_bt_uuid_t){
                .len = ESP_UUID_LEN_16, .uuid = {.uuid16 = UUID_TEMP_CHAR}},
                ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ, NULL, NULL);

            esp_ble_gatts_add_char(env_profile.service_handle, &(esp_bt_uuid_t){
                .len = ESP_UUID_LEN_16, .uuid = {.uuid16 = UUID_HUMID_CHAR}},
                ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ, NULL, NULL);

            esp_ble_gatts_start_service(env_profile.service_handle);
        } else if (param->create.service_id.id.uuid.uuid.uuid16 == UUID_BATTERY_SERVICE) {
            battery_profile.service_handle = param->create.service_handle;

            esp_ble_gatts_add_char(battery_profile.service_handle, &(esp_bt_uuid_t){
                .len = ESP_UUID_LEN_16, .uuid = {.uuid16 = UUID_BATTERY_CHAR}},
                ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ, NULL, NULL);

            esp_ble_gatts_start_service(battery_profile.service_handle);
        }
        break;

    case ESP_GATTS_READ_EVT:
    esp_gatt_rsp_t rsp;
    memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
    rsp.attr_value.handle = param->read.handle;

    if (param->read.handle == env_profile.temp_char_handle) {
        int16_t temp = (int16_t)(temperature * 10); // Temperatura w setnych stopnia
        rsp.attr_value.value[0] = temp & 0xFF;
        rsp.attr_value.value[1] = (temp >> 8) & 0xFF;
        rsp.attr_value.len = 2;
    } else if (param->read.handle == env_profile.humid_char_handle) {
        int16_t hum = (int16_t)(humidity * 100); // Wilgotność w setnych procentach
        rsp.attr_value.value[0] = hum & 0xFF;
        rsp.attr_value.value[1] = (hum >> 8) & 0xFF;
        rsp.attr_value.len = 2;
    } else if (param->read.handle == battery_profile.battery_char_handle) {
        rsp.attr_value.value[0] = battery_level;
        rsp.attr_value.len = 1;
    }

    esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
    break;


    default:
        break;
    }
}

// Funkcja konfiguracji reklamowania BLE
static void setup_ble_adv() {
    esp_ble_adv_data_t adv_data = {
        .set_scan_rsp = false,
        .include_name = true,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
    };

    esp_ble_adv_params_t adv_params = {
        .adv_int_min = 0x20,
        .adv_int_max = 0x40,
        .adv_type = ADV_TYPE_IND,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .channel_map = ADV_CHNL_ALL,
        .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };

    esp_ble_gap_config_adv_data(&adv_data);
    esp_ble_gap_start_advertising(&adv_params);
}

void app_main(void) {
    esp_err_t ret;

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