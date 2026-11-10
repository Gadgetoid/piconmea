#ifndef _PICONMEA_GPSVIEW_H
#define _PICONMEA_GPSVIEW_H

#include <stdbool.h>

#include "nmea.h"

bool gpsview_init(void);
void gpsview_render(const nmea_state_t *state, bool simulating, bool bluetooth_connected);

#endif
