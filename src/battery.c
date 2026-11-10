#include "battery.h"

#include PICONMEA_BOARD_HEADER

#ifdef BATTERY_SENSE_PIN

#include "hardware/adc.h"
#include "pico/cyw43_arch.h"

#define ADC_REFERENCE 3.3f
#define ADC_FULL_SCALE 4096.0f
#define SAMPLES 16

typedef struct {
    float voltage;
    int percent;
} discharge_point_t;

static const discharge_point_t discharge_curve[] = {
    {4.20f, 100}, {4.15f, 95}, {4.11f, 90}, {4.08f, 85}, {4.02f, 80}, {3.98f, 75}, {3.95f, 70},
    {3.91f, 65}, {3.87f, 60}, {3.85f, 55}, {3.84f, 50}, {3.82f, 45}, {3.80f, 40}, {3.79f, 35},
    {3.77f, 30}, {3.75f, 25}, {3.73f, 20}, {3.71f, 15}, {3.69f, 10}, {3.61f, 5}, {3.27f, 0},
};

static int estimate_percent(float voltage) {
    size_t count = sizeof(discharge_curve) / sizeof(discharge_curve[0]);
    if (voltage >= discharge_curve[0].voltage) {
        return 100;
    }
    for (size_t i = 1; i < count; i++) {
        const discharge_point_t *upper = &discharge_curve[i - 1];
        const discharge_point_t *lower = &discharge_curve[i];
        if (voltage >= lower->voltage) {
            float fraction = (voltage - lower->voltage) / (upper->voltage - lower->voltage);
            return lower->percent + (int)(fraction * (float)(upper->percent - lower->percent) + 0.5f);
        }
    }
    return 0;
}

void battery_init(void) {
    adc_init();
    adc_gpio_init(BATTERY_SENSE_PIN);
}

battery_status_t battery_read(void) {
    adc_select_input(BATTERY_SENSE_PIN - ADC_BASE_PIN);
    uint32_t total = 0;
    for (int i = 0; i < SAMPLES; i++) {
        total += adc_read();
    }
    float voltage = (float)total / SAMPLES * ADC_REFERENCE / ADC_FULL_SCALE * BATTERY_DIVIDER;
    return (battery_status_t){
        .present = true,
        .voltage = voltage,
        .percent = estimate_percent(voltage),
        .usb_power = cyw43_arch_gpio_get(BATTERY_VBUS_WL_GPIO),
    };
}

#else

void battery_init(void) {
}

battery_status_t battery_read(void) {
    return (battery_status_t){0};
}

#endif
