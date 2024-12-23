#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "ble_sensor.h"

#define GATTC_TAG "GATTC"

// UUID dla usług i charakterystyk
#define ENVIRONMENT_SERVICE_UUID 0x181A 
#define TEMPERATURE_CHAR_UUID  0x2A1F
#define HUMIDITY_CHAR_UUID 0x2A6F

#define BATTERY_SERVICE_UUID 0x180F
#define BATTERY_LEVEL_CHAR_UUID 0x2A19



uint16_t humidity_char_handle = 0;
uint16_t battery_char_handle = 0;
uint16_t battery_service_start_handle = 0;
uint16_t battery_service_end_handle = 0;


float current_temperature_ble = 0.0; // Przechowuje odczytaną temperaturę
float current_humidity_ble = 0.0;    // Przechowuje odczytaną wilgotność


static bool is_connected = false; // czy zostało nawiązane połączenie
static bool connection_in_progress = false; // czy jest wykonywana teraz próba nawiązania połączenia
static esp_gattc_char_elem_t *char_elem_result = NULL; // przechowuje wyniki wyszukiwania charakterystyk GATT
static esp_gattc_descr_elem_t *descr_elem_result = NULL; // przechowuje wyniki wyszukiwania deskryptorów GATT
static const char device_name[] = "ATC_4BEDDC"; // termometr

// Deskryptor powiadomień (pozwala włączyć/wyłączyć)
static esp_bt_uuid_t notify_descr_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG,},
};
// Parametry skanowania urządzeń BLE
static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE, // skanowanie aktywne (wysyła zapytania)
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL, // bez filtrów - odbiera reklamy od wszystkich
    .scan_interval          = 0x30,  // czas między kolejnymi cyklami skanowania: 30 ms
    .scan_window            = 0x20, // czas trwania aktywnego skanowania w każdym cyklu: 30 ms
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE // wyłączenie filtracji duplikatów - klient może odbierać reklamy wielokrotnie z tego samego urządzenia
};

struct gattc_profile_inst {
    esp_gattc_cb_t gattc_cb;   // Callback profilu
    uint16_t gattc_if;     // ID klienta    
    uint16_t app_id;     // ID aplikacji BLE klienta      
    uint16_t conn_id;        // ID połączenia 
    uint16_t service_start_handle;  // Początek uchwytu usługi
    uint16_t service_end_handle;    // Koniec uchwytu usługi
    uint16_t char_handle;           // Uchwyty charakterystyk
    esp_bd_addr_t remote_bda;       // Adres urządzenia BLE
};

// Instancja profilu
static struct gattc_profile_inst gattc_profile = {
    .gattc_cb = gattc_profile_event_handler, // Funkcja obsługująca zdarzenia w profilu
    .gattc_if = ESP_GATT_IF_NONE,           // Domyślna wartość (brak interfejsu przypisanego)
};

// Obsługa zdarzeń GATT w profilu klienta BLE
// Parametry: typ zdarzenia, interfejs GATT przypisany do klienta, wskaźnik na strukturę zawierającą szczegółowe dane związane ze zdarzeniem
void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    // Przypisuje wskaźnik do danych wejściowych, aby ułatwić dostęp do parametrów zdarzenia.
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;

    switch (event) {
         ESP_LOGI(GATTC_TAG, "ESP_GATTC_REG_FOR_NOTIFY_EVT, status = %d", p_data->reg_for_notify.status);
        // Zakończona rejestracja dla powiadomień danej charakterystyki
        case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
            ESP_LOGI(GATTC_TAG, "Temperature handle: %d, Humidity handle: %d, Battery handle: %d",
             gattc_profile.char_handle, humidity_char_handle, battery_char_handle);
            if (p_data->reg_for_notify.status != ESP_GATT_OK) { // Sprawdzenie statusu rejestracji powiadomień
                ESP_LOGE(GATTC_TAG, "REG FOR NOTIFY failed: error status = %d", p_data->reg_for_notify.status);
                break;
            }

            // Pobranie liczby deskryptorów powiązanych z charakterystyką
            uint16_t count = 0;
            uint16_t notify_en = 1; // Wartość aktywująca powiadomienia 
            esp_gatt_status_t ret_status = esp_ble_gattc_get_attr_count( // oblicza liczbę deskryptorów w charakterystyce
                gattc_if, // interfejs GATT klienta
                gattc_profile.conn_id,  // ID połączenia BLE
                ESP_GATT_DB_DESCRIPTOR,     // filtruje deskryptory w bazie danych GATT
                gattc_profile.service_start_handle, // zakres uchwytów usługi, w której znajduje się charakterystyka
                gattc_profile.service_end_handle,
                p_data->reg_for_notify.handle, // Uchwyt charakterystyki, dla której szukane są deskryptoryy
                &count); 
    
            if (ret_status != ESP_GATT_OK || count == 0) { // Nie znaleziono dekryptorów lub wystąpił błąd
                ESP_LOGE(GATTC_TAG, "No descriptors found for handle: %d", p_data->reg_for_notify.handle);
                break;
            }

            // Alokuje pamięć dla tablicy przechowującej informację o deskryptorach
            descr_elem_result = malloc(sizeof(esp_gattc_descr_elem_t) * count);
            if (!descr_elem_result) {
                ESP_LOGE(GATTC_TAG, "Failed to allocate memory for descriptors");
                break;
            }
            // Pobranie deskryptorów charakterystyki
            ret_status = esp_ble_gattc_get_descr_by_char_handle(
                gattc_if,
                gattc_profile.conn_id,
                p_data->reg_for_notify.handle,
                notify_descr_uuid, // UUID deskryptora
                descr_elem_result, // Wskaźnik na tablicę wynikową
                &count);
            if (ret_status != ESP_GATT_OK || count == 0) {
                ESP_LOGE(GATTC_TAG, "Failed to get descriptors by char handle, error status = %d", ret_status);
                free(descr_elem_result);
                descr_elem_result = NULL;
                break;
            }

            // Włączenie powiadomień
            ret_status = esp_ble_gattc_write_char_descr(
                gattc_if,
                gattc_profile.conn_id,
                descr_elem_result[0].handle, // Uchwyt deskryptora CCCD
                sizeof(notify_en),           
                (uint8_t *)&notify_en,       // Wskaźnik do wartości
                ESP_GATT_WRITE_TYPE_RSP,     // Typ zapisu (response)
                ESP_GATT_AUTH_REQ_NONE);     // Brak wymagań autoryzacji
            if (ret_status != ESP_OK) {
                ESP_LOGE(GATTC_TAG, "Failed to write to descriptor, error = %s", esp_err_to_name(ret_status));
            } else {
                
                ESP_LOGI(GATTC_TAG, "Notifications enabled for handle: %d", descr_elem_result[0].handle);
            }
            free(descr_elem_result);
            descr_elem_result = NULL;
            ESP_LOGI(GATTC_TAG, "Notifications registered successfully");
            break;
        }

        // Zakończenie rejestracji klienta
        case ESP_GATTC_REG_EVT:
            esp_err_t scan_ret = esp_ble_gap_set_scan_params(&ble_scan_params); // ustawienie parametrów skanowania BLE
            if (scan_ret){
                ESP_LOGE(GATTC_TAG, "set scan params error, error code = %x", scan_ret);
            }
            // Po poprawnym ustawieniu parametrów skanowania można rozpocząć wyszukiwanie urządzeń BLE
            break;

        // Urządzenie nawiązuje połączenie BLE
       case ESP_GATTC_CONNECT_EVT: {
            ESP_LOGI(GATTC_TAG, "Connected to server.");
            esp_log_buffer_hex(GATTC_TAG, p_data->connect.remote_bda, sizeof(esp_bd_addr_t)); // Logowanie adresu serwera

            // Przechowywanie informacji o połączeniu
            gattc_profile.conn_id = p_data->connect.conn_id;
            memcpy(gattc_profile.remote_bda, p_data->connect.remote_bda, sizeof(esp_bd_addr_t));

            vTaskDelay(pdMS_TO_TICKS(1000));
            // Wysłanie żądania MTU
            esp_err_t mtu_ret = esp_ble_gattc_send_mtu_req(gattc_if, p_data->connect.conn_id);
            if (mtu_ret != ESP_OK) {
                ESP_LOGE(GATTC_TAG, "Failed to send MTU request: %s", esp_err_to_name(mtu_ret));
            }

            // parametry połączenia
            esp_ble_conn_update_params_t conn_params = {
                .latency = 0,               // Brak opóźnień
                .max_int = 0x50,            // Max interwał
                .min_int = 0x30,            // Min interwał 
                .timeout = 2000              // Timeout 20s
            };
            memcpy(conn_params.bda, p_data->connect.remote_bda, sizeof(esp_bd_addr_t));
            esp_err_t conn_update_ret = esp_ble_gap_update_conn_params(&conn_params);
            if (conn_update_ret != ESP_OK) {
                ESP_LOGE(GATTC_TAG, "Failed to update connection parameters: %s", esp_err_to_name(conn_update_ret));
            }
            vTaskDelay(pdMS_TO_TICKS(3000));
            break;
        }




        // Otwieranie połączenia GATT
        case ESP_GATTC_OPEN_EVT:
            if (param->open.status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "open failed, status %d", p_data->open.status);
                break;
            }
            ESP_LOGI(GATTC_TAG, "open success");
          
            break;

        // Zakończono odkrywanie usług GATT
        case ESP_GATTC_DIS_SRVC_CMPL_EVT:
            if (param->dis_srvc_cmpl.status != ESP_GATT_OK) {
                ESP_LOGE(GATTC_TAG, "Discover service failed, status %d", param->dis_srvc_cmpl.status);
                break;
            }
            // Przeszukanie wszystkich dostępnych usług
            esp_err_t status = esp_ble_gattc_search_service(gattc_if, param->dis_srvc_cmpl.conn_id, NULL);
            if (status != ESP_OK) {
                ESP_LOGE(GATTC_TAG, "Failed to search services, error %d", status);
            }
            break;

        // Konfiguracja maksymalnego rozmiaru danych przesyłanego w jednym pakiecie
        case ESP_GATTC_CFG_MTU_EVT:
            if (param->cfg_mtu.status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "Config MTU failed, error status = %x", param->cfg_mtu.status);
            } 
            break;


        // Znaleziono usługę
        case ESP_GATTC_SEARCH_RES_EVT:
            if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16) { // Czy UUID ma 16 bitów
                uint16_t uuid = p_data->search_res.srvc_id.uuid.uuid.uuid16;
                if (uuid == ENVIRONMENT_SERVICE_UUID) {
                    ESP_LOGI(GATTC_TAG, "Environmental Sensing Service found.");
                    // Zapisanie uchwytów usługi
                    gattc_profile.service_start_handle = p_data->search_res.start_handle;
                    gattc_profile.service_end_handle = p_data->search_res.end_handle;
                } else if (uuid == BATTERY_SERVICE_UUID) {
                    ESP_LOGI(GATTC_TAG, "Battery Service found.");
                    battery_service_start_handle = p_data->search_res.start_handle;
                    battery_service_end_handle = p_data->search_res.end_handle;
                }
                ESP_LOGI(GATTC_TAG, "Temperature handle: %d, Humidity handle: %d, Battery handle: %d",
                 gattc_profile.char_handle, humidity_char_handle, battery_char_handle);
            }
            break;

        // Wyszukiwanie usług zostało zakończone
        case ESP_GATTC_SEARCH_CMPL_EVT:
            if (p_data->search_cmpl.status != ESP_GATT_OK) {
                ESP_LOGE(GATTC_TAG, "Search service failed, error status = %x", p_data->search_cmpl.status);
                break;
            }
            ESP_LOGI(GATTC_TAG, "Search complete, services discovered.");
            uint8_t ready_flag = 1;
            esp_err_t write_ret = esp_ble_gattc_write_char(
                gattc_if,
                gattc_profile.conn_id,
                gattc_profile.char_handle, // Uchwytem powinna być charakterystyka gotowości
                sizeof(ready_flag),
                &ready_flag,
                ESP_GATT_WRITE_TYPE_RSP,
                ESP_GATT_AUTH_REQ_NONE);

            if (write_ret != ESP_OK) {
                ESP_LOGE(GATTC_TAG, "Failed to write to ready characteristic: %s", esp_err_to_name(write_ret));
            } else {
                ESP_LOGI(GATTC_TAG, "Client is ready, written to characteristic.");
            }
            // Wyszukiwanie charakterystyk w Environmental Sensing Service
            if (gattc_profile.service_start_handle && // Jeśli uchwyty są ustawione, to usługa została znaleziona
                gattc_profile.service_end_handle) {

                    ESP_LOGI(GATTC_TAG, "Found service handles: start=%d, end=%d",
                 gattc_profile.service_start_handle, gattc_profile.service_end_handle);

                uint16_t count = 0;
                // Wyznaczenie liczby charakterystyk
                esp_ble_gattc_get_attr_count(gattc_if, p_data->search_cmpl.conn_id,
                                            ESP_GATT_DB_CHARACTERISTIC,
                                            gattc_profile.service_start_handle,
                                            gattc_profile.service_end_handle,
                                            0, &count);
                if (count > 0) { // Jeśli znaleziono jakieś charakterystyki w usłudze
                    char_elem_result = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
                    if (!char_elem_result) {
                        ESP_LOGE(GATTC_TAG, "No memory for char_elem_result");
                        break;
                    }
                    // Charakterystyka temperatury
                    esp_bt_uuid_t temp_char_uuid = {
                        .len = ESP_UUID_LEN_16,
                        .uuid = {.uuid16 = TEMPERATURE_CHAR_UUID}
                    };
                    esp_gatt_status_t status = esp_ble_gattc_get_char_by_uuid(
                        gattc_if,
                        p_data->search_cmpl.conn_id,
                        gattc_profile.service_start_handle,
                        gattc_profile.service_end_handle,
                        temp_char_uuid,
                        char_elem_result,
                        &count);
                    if (status == ESP_GATT_OK && count > 0) { // Jeśli znaleziono, to zapisuje uchwyt i rejestruje powiadomienia
                        gattc_profile.char_handle = char_elem_result[0].char_handle;
                        esp_ble_gattc_register_for_notify(
                            gattc_if,
                            gattc_profile.remote_bda,
                            char_elem_result[0].char_handle);
                    } else {
                        ESP_LOGE(GATTC_TAG, "Temperature characteristic not found");
                    }

                    // Charakterystyka wilgotności
                    esp_bt_uuid_t hum_char_uuid = {
                        .len = ESP_UUID_LEN_16,
                        .uuid = {.uuid16 = HUMIDITY_CHAR_UUID}
                    };
                    status = esp_ble_gattc_get_char_by_uuid(
                        gattc_if,
                        p_data->search_cmpl.conn_id,
                        gattc_profile.service_start_handle,
                        gattc_profile.service_end_handle,
                        hum_char_uuid,
                        char_elem_result,
                        &count);
                    if (status == ESP_GATT_OK && count > 0) {
                        humidity_char_handle = char_elem_result[0].char_handle;
                        esp_ble_gattc_register_for_notify(
                            gattc_if,
                            gattc_profile.remote_bda,
                            humidity_char_handle);
                    } else {
                        ESP_LOGE(GATTC_TAG, "Humidity characteristic not found");
                    }
                    ESP_LOGI(GATTC_TAG, "Temperature handle: %d, Humidity handle: %d, Battery handle: %d",
                     gattc_profile.char_handle, humidity_char_handle, battery_char_handle);
                    free(char_elem_result);
                }
            }

            // Szukanie charakterystyk w Battery Service
            if (battery_service_start_handle && battery_service_end_handle) {
                uint16_t count = 0;
                esp_gatt_status_t status = esp_ble_gattc_get_attr_count(
                    gattc_if,
                    p_data->search_cmpl.conn_id,
                    ESP_GATT_DB_CHARACTERISTIC,
                    battery_service_start_handle,
                    battery_service_end_handle,
                    0,
                    &count);
                if (status == ESP_GATT_OK && count > 0) {
                    char_elem_result = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
                    if (!char_elem_result) {
                        ESP_LOGE(GATTC_TAG, "No memory for char_elem_result");
                        break;
                    }
                    esp_bt_uuid_t battery_char_uuid = {
                        .len = ESP_UUID_LEN_16,
                        .uuid = {.uuid16 = BATTERY_LEVEL_CHAR_UUID},
                    };
                    // Charakterystyka Battery Level
                    status = esp_ble_gattc_get_char_by_uuid(
                        gattc_if,
                        p_data->search_cmpl.conn_id,
                        battery_service_start_handle,
                        battery_service_end_handle,
                        battery_char_uuid,
                        char_elem_result,
                        &count);

                    if (status == ESP_GATT_OK && count > 0) {
                        battery_char_handle = char_elem_result[0].char_handle;
                        // Rejestracja powiadomień dla Battery Level
                        esp_ble_gattc_register_for_notify(
                            gattc_if,
                            gattc_profile.remote_bda,
                            battery_char_handle);
                    } else {
                        ESP_LOGE(GATTC_TAG, "Battery Level characteristic not found");
                    }
                    free(char_elem_result);
                } else {
                    ESP_LOGE(GATTC_TAG, "Battery Service characteristic not found");
                }
            }
            break;

        // Odbieranie danych z powiadomień od serwera 
        case ESP_GATTC_NOTIFY_EVT:
        ESP_LOGI(GATTC_TAG, "Received notification for handle: %d", p_data->notify.handle);
            if (p_data->notify.handle == gattc_profile.char_handle) { // Sprawdzenie, do której charakterystyki należy powiadomienie
                // Przetwarzanie danych
                int16_t raw_temp = (p_data->notify.value[1] << 8) | p_data->notify.value[0];
                current_temperature_ble = raw_temp / 10.0;
                ESP_LOGI(GATTC_TAG, "Received notification: Temperature: %.2f°C", current_temperature_ble);
            } else if (p_data->notify.handle == humidity_char_handle) {
                int16_t raw_hum = (p_data->notify.value[1] << 8) | p_data->notify.value[0];
                current_humidity_ble = raw_hum / 100.0;
                ESP_LOGI(GATTC_TAG, "Received notification: Humidity: %.2f%%", current_humidity_ble);
            } else if (p_data->notify.handle == battery_char_handle) {
                uint8_t battery_level = p_data->notify.value[0];
                ESP_LOGI(GATTC_TAG, "Received notification: Battery Level: %d%%", battery_level);
            }
            break;

        // Zmiana usług GATT
        case ESP_GATTC_SRVC_CHG_EVT: {
            esp_bd_addr_t bda;
            memcpy(bda, p_data->srvc_chg.remote_bda, sizeof(esp_bd_addr_t));
            ESP_LOGI(GATTC_TAG, "ESP_GATTC_SRVC_CHG_EVT, bd_addr:");
            esp_log_buffer_hex(GATTC_TAG, bda, sizeof(esp_bd_addr_t)); // logowanie adresu urządzenia
            break;
        }
  
        // Rozłączenie BLE
        case ESP_GATTC_DISCONNECT_EVT:
            is_connected = false;
            ESP_LOGI(GATTC_TAG, "ESP_GATTC_DISCONNECT_EVT, reason = %d", p_data->disconnect.reason);
            connection_in_progress = false;
            ESP_LOGI(GATTC_TAG, "Disconnected, restarting scan...");
            esp_ble_gap_start_scanning(60);esp_ble_gap_start_scanning(60);
            
            break;

        default:
            break;
    }
}

// Obsługuje zdarzenia GAP
void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    uint8_t *adv_name = NULL;
    uint8_t adv_name_len = 0;
    switch (event) {

    // Parametry skanowania zostały ustawione, rozpoczęcie skanowania
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
        ESP_LOGI(GATTC_TAG, "Scan parameters set.");
        uint32_t duration = 60; // czas trwania skanowania: 60 s
        esp_ble_gap_start_scanning(duration);
        break;
    }

    // Skanowanie rozpoczęte
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTC_TAG, "scan start failed, error status = %x", param->scan_start_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "scan start success");

        break;
    
    // Obsługiwanie wyników skanowania
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        uint8_t *adv_name = NULL;
        uint8_t adv_name_len = 0;

        // Logowanie adresu MAC urządzenia
        esp_log_buffer_hex(GATTC_TAG, scan_result->scan_rst.bda, 6);

        // Logowanie nazwy urządzenia
        adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                            ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
        

        // Sprawdzenie zgodności nazwy urządzenia
        if (adv_name && strncmp((char *)adv_name, device_name, adv_name_len) == 0) {
            ESP_LOGI(GATTC_TAG, "Matched device: %.*s", adv_name_len, adv_name);
            ESP_LOGI(GATTC_TAG, "Connecting to device with address:");
            esp_log_buffer_hex(GATTC_TAG, scan_result->scan_rst.bda, 6);

            if (!connection_in_progress && !is_connected) {
                connection_in_progress = true;  // połączenie jest w trakcie
                esp_ble_gap_stop_scanning();   // Zatrzymaj skanowanie
                esp_err_t open_ret = esp_ble_gattc_open(gattc_profile.gattc_if, 
                                                        scan_result->scan_rst.bda,
                                                        scan_result->scan_rst.ble_addr_type, 
                                                        true);
                if (open_ret != ESP_OK) {
                    ESP_LOGE(GATTC_TAG, "Failed to open connection: %s", esp_err_to_name(open_ret));
                    connection_in_progress = false;  // Zresetuj flagę w przypadku błędu
                } else {
                    ESP_LOGI(GATTC_TAG, "Connection initiated.");
                }
            } else {
                ESP_LOGI(GATTC_TAG, "Already connecting or connected to a device, skipping...");
            }
        }
        break;
}




    // Zakończenie skanowania urządzeń
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "scan stop failed, error status = %x", param->scan_stop_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "stop scan successfully");
        break;

    default:
        break;
    }
}

// Główny callback obsługujący zdarzenia GATT związane z klientem BLE
// Parametry: typ zdarzenia GATT, interfejs GATT klienta (id przypisany po rejestracji), wskaźnik do struktury zawierającej szczegółowe informacje o zdarzeniu
void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    // Obsługa zdarzenia rejestracji
    if (event == ESP_GATTC_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gattc_profile.gattc_if = gattc_if; // Zapisuje interfejs gattc_if do globalnej struktury gattc_profile
        } else {
            ESP_LOGI(GATTC_TAG, "reg app failed, app_id %04x, status %d",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }    
    do { // Przekierowanie zdarzeń do callbacka profilu
        if (gattc_if == ESP_GATT_IF_NONE || gattc_if == gattc_profile.gattc_if) { // Wysyłanie zdarzeń do funkcji gattc_profile.gattc_cb, jeśli zdarzenie dotyczy zarejestrowanego klienta
            if (gattc_profile.gattc_cb) { 
                gattc_profile.gattc_cb(event, gattc_if, param);
            }
        }
        
    } while (0);
}
