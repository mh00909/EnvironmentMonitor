#include "light_sensor.h"
#include "driver/adc.h"
#include <stdio.h>
#include <math.h>

int current_light = 0;

// Inicjalizacja fotorezystora
void light_sensor_init(void) {
    // Konfiguracja ADC1
    adc1_config_width(ADC_WIDTH_BIT_12); // 12-bitowa rozdzielczość
    adc1_config_channel_atten(LIGHT_SENSOR_ADC_CHANNEL, ADC_ATTEN_DB_11); // Tłumienie 11 dB dla zakresu 0–3,9V
}

// Odczyt wartości lux
void light_sensor_read(int* read) {
    // Sprawdzenie wskaźnika (dla bezpieczeństwa)
    if (read == NULL) {
        return;
    }

    int adc_value = adc1_get_raw(LIGHT_SENSOR_ADC_CHANNEL);
    float v_out = (adc_value / ADC_MAX_VALUE) * V_REF;

    // Oblicz rezystancję fotorezystora
    float r_ldr = R_PULLDOWN * ((V_REF - v_out) / v_out);

    // Oblicz natężenie światła w luksach
    float lux = A * pow(r_ldr / R_PULLDOWN, -N);

    // Pobranie surowej wartości ADC i zapisanie w adresie wskaźnika
    *read = lux;
}

// Konwersja wartości ADC na procent jasności
float light_sensor_to_percentage(uint16_t adc_value) {
    // Przeliczenie wartości ADC na procent jasności
    return (1.0f - ((float)adc_value / 4095.0f)) * 100.0f;
}

