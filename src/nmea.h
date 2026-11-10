#ifndef _PICONMEA_NMEA_H
#define _PICONMEA_NMEA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NMEA_MAX_SATELLITES 64

typedef enum {
    CONSTELLATION_GPS,
    CONSTELLATION_GLONASS,
    CONSTELLATION_GALILEO,
    CONSTELLATION_BEIDOU,
    CONSTELLATION_OTHER,
    CONSTELLATION_COUNT
} constellation_t;

typedef struct {
    constellation_t constellation;
    uint16_t prn;
    int16_t elevation;
    int16_t azimuth;
    int16_t snr;
    bool used;
    uint32_t seen_ms;
} nmea_satellite_t;

typedef struct {
    bool position_valid;
    uint8_t fix_quality;
    uint8_t fix_mode;
    double latitude;
    double longitude;
    float altitude;
    float speed_knots;
    float course;
    float pdop;
    float hdop;
    float vdop;
    uint8_t satellites_used;
    bool time_valid;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    nmea_satellite_t satellites[NMEA_MAX_SATELLITES];
    size_t satellite_count;
} nmea_state_t;

bool nmea_feed(const char *sentence, size_t length, uint32_t now_ms);
void nmea_expire(uint32_t now_ms, uint32_t max_age_ms);
const nmea_state_t *nmea_state(void);

#endif
