#ifndef _PICONMEA_BATTERY_H
#define _PICONMEA_BATTERY_H

#include <stdbool.h>

typedef struct {
    bool present;
    float voltage;
    int percent;
    bool usb_power;
} battery_status_t;

void battery_init(void);
battery_status_t battery_read(void);

#endif
