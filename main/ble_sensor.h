#ifndef BLE_SENSOR_H
#define BLE_SENSOR_H
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"
//#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Deklaracje zmiennych globalnych dla temperatury i wilgotności
extern float current_temperature_ble;
extern float current_humidity_ble;
extern bool ble_connected;

void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
esp_err_t read_ble_data();

// Funkcja inicjalizująca BLE
esp_err_t ble_initialize(void);

bool esp_ble_gap_is_scanning();

#endif // BLE_SENSOR_H
