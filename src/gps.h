#ifndef _PICONMEA_GPS_H
#define _PICONMEA_GPS_H

#include <stddef.h>
#include <stdint.h>

typedef void (*gps_sentence_handler_t)(const char *sentence, size_t length);

void gps_init(gps_sentence_handler_t handler);
void gps_poll(void);
void gps_write(const uint8_t *data, size_t length);

#endif
