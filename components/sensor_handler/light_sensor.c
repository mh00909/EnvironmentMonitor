#include "light_sensor.h"
#include "driver/adc.h"
#include <stdio.h>
#include "esp_log.h"
#include <math.h>

int current_light = 0;

// Inicjalizacja fotorezystora
void light_sensor_init(void) {
    // Konfiguracja ADC1
    adc1_config_width(ADC_WIDTH_BIT_12); 
    adc1_config_channel_atten(LIGHT_SENSOR_ADC_CHANNEL, ADC_ATTEN_DB_11); // Tłumienie 11 dB dla zakresu 0–3,9V
}

void light_sensor_read(int* read) {
    if (read == NULL) {
        return;
    }

    int adc_value = adc1_get_raw(LIGHT_SENSOR_ADC_CHANNEL); // surowa wartość napięcia
    float v_out = (adc_value / ADC_MAX_VALUE) * V_REF; // rzeczywiste napięcie na fotorezystorze

    if (v_out <= 0.01 || v_out >= V_REF - 0.01) { 
        *read = 0;
        current_light = 0;
        return;
    }

    float r_ldr = R_PULLDOWN * ((V_REF - v_out) / v_out); // rezystancja fotorezystora
    float lux = A * pow(R_PULLDOWN / r_ldr, N);

    *read = (int)lux;
    current_light = (int)lux;
    ESP_LOGI("LIGHT_SENSOR", "ADC: %d, V_out: %.2f V, R_LDR: %.2f Ohm, Lux: %.2f", adc_value, v_out, r_ldr, lux);
}


// Konwersja wartości luksów na procent jasności
float light_sensor_to_percentage(uint16_t lux_value) {
    float max_lux = 1000.0f; // Ustaw maksymalne natężenie światła w luksach
    if (lux_value > max_lux) lux_value = max_lux; 
    return (lux_value / max_lux) * 100.0f; // Przelicz na procent
}
