#include "power_manager.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_sleep.h"

static const char *TAG = "POWER_MANAGER";
static PowerMode current_mode = MODE_STANDARD;

void set_power_mode(PowerMode mode) {
    current_mode = mode;
    ESP_LOGI(TAG, "Ustawiono tryb pracy: %s", (mode == MODE_POWER_SAVE) ? "OSZCZĘDZANIE" : "STANDARDOWY");
}

void power_manager_task(void *pvParameters) {
    while (1) {
        if (current_mode == MODE_POWER_SAVE) {
            ESP_LOGI(TAG, "Przechodzenie w tryb oszczędzania energii.");
            esp_wifi_stop();
            esp_sleep_enable_timer_wakeup(60 * 1000000); // Uśpienie na 60 sekund
            esp_deep_sleep_start();
        } else {
            ESP_LOGI(TAG, "Praca w trybie standardowym.");
            vTaskDelay(pdMS_TO_TICKS(1000)); // Standardowe opóźnienie
        }
    }
}
