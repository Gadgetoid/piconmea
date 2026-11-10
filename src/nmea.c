#include <stdlib.h>
#include <string.h>

#include "nmea.h"

#define MAX_FIELDS 24
#define SENTENCE_MAX 128
#define KNOTS_TO_KMH 1.852f
#define GLONASS_FIRST_PRN 65
#define GLONASS_LAST_PRN 96

static nmea_state_t state;

static int hex_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static bool checksum_valid(const char *sentence, size_t length) {
    const char *star = memchr(sentence, '*', length);
    if (!star || star + 2 >= sentence + length) {
        return false;
    }
    uint8_t checksum = 0;
    for (const char *character = sentence + 1; character < star; character++) {
        checksum ^= (uint8_t)*character;
    }
    int high = hex_value(star[1]);
    int low = hex_value(star[2]);
    return high >= 0 && low >= 0 && checksum == (uint8_t)((high << 4) | low);
}

static size_t split_fields(char *body, char **fields) {
    size_t count = 0;
    fields[count++] = body;
    for (char *character = body; *character && count < MAX_FIELDS; character++) {
        if (*character == ',') {
            *character = '\0';
            fields[count++] = character + 1;
        }
    }
    return count;
}

static constellation_t talker_constellation(const char *talker) {
    if (strncmp(talker, "GP", 2) == 0) {
        return CONSTELLATION_GPS;
    }
    if (strncmp(talker, "GL", 2) == 0) {
        return CONSTELLATION_GLONASS;
    }
    if (strncmp(talker, "GA", 2) == 0) {
        return CONSTELLATION_GALILEO;
    }
    if (strncmp(talker, "GB", 2) == 0 || strncmp(talker, "BD", 2) == 0) {
        return CONSTELLATION_BEIDOU;
    }
    return CONSTELLATION_OTHER;
}

static constellation_t system_id_constellation(int system_id) {
    switch (system_id) {
        case 1:
            return CONSTELLATION_GPS;
        case 2:
            return CONSTELLATION_GLONASS;
        case 3:
            return CONSTELLATION_GALILEO;
        case 4:
            return CONSTELLATION_BEIDOU;
        default:
            return CONSTELLATION_OTHER;
    }
}

static double parse_coordinate(const char *value, const char *hemisphere) {
    if (!*value || !*hemisphere) {
        return 0.0;
    }
    double raw = atof(value);
    int degrees = (int)(raw / 100);
    double result = degrees + (raw - degrees * 100) / 60.0;
    return (*hemisphere == 'S' || *hemisphere == 'W') ? -result : result;
}

static void parse_time(const char *value) {
    if (strlen(value) < 6) {
        return;
    }
    state.hour = (uint8_t)((value[0] - '0') * 10 + (value[1] - '0'));
    state.minute = (uint8_t)((value[2] - '0') * 10 + (value[3] - '0'));
    state.second = (uint8_t)((value[4] - '0') * 10 + (value[5] - '0'));
    state.time_valid = true;
}

static nmea_satellite_t *find_satellite(constellation_t constellation, uint16_t prn, uint32_t now_ms) {
    for (size_t i = 0; i < state.satellite_count; i++) {
        if (state.satellites[i].constellation == constellation && state.satellites[i].prn == prn) {
            return &state.satellites[i];
        }
    }
    if (state.satellite_count >= NMEA_MAX_SATELLITES) {
        return NULL;
    }
    nmea_satellite_t *satellite = &state.satellites[state.satellite_count++];
    *satellite = (nmea_satellite_t){constellation, prn, -1, -1, -1, false, now_ms};
    return satellite;
}

static void parse_gga(char **fields, size_t count) {
    if (count < 10) {
        return;
    }
    parse_time(fields[1]);
    state.fix_quality = (uint8_t)atoi(fields[6]);
    state.satellites_used = (uint8_t)atoi(fields[7]);
    if (*fields[8]) {
        state.hdop = (float)atof(fields[8]);
    }
    if (state.fix_quality) {
        state.latitude = parse_coordinate(fields[2], fields[3]);
        state.longitude = parse_coordinate(fields[4], fields[5]);
        state.altitude = (float)atof(fields[9]);
    }
}

static void parse_rmc(char **fields, size_t count) {
    if (count < 9) {
        return;
    }
    parse_time(fields[1]);
    state.position_valid = fields[2][0] == 'A';
    if (state.position_valid) {
        state.latitude = parse_coordinate(fields[3], fields[4]);
        state.longitude = parse_coordinate(fields[5], fields[6]);
        state.speed_kmh = (float)atof(fields[7]) * KNOTS_TO_KMH;
        state.course = (float)atof(fields[8]);
    }
}

static void parse_gsa(const char *talker, char **fields, size_t count, uint32_t now_ms) {
    if (count < 18) {
        return;
    }
    state.fix_mode = (uint8_t)atoi(fields[2]);
    state.pdop = (float)atof(fields[15]);
    state.hdop = (float)atof(fields[16]);
    state.vdop = (float)atof(fields[17]);

    constellation_t constellation = talker_constellation(talker);
    if (count >= 19 && *fields[18]) {
        constellation = system_id_constellation(atoi(fields[18]));
    } else if (constellation == CONSTELLATION_OTHER) {
        int first_prn = atoi(fields[3]);
        constellation = first_prn >= GLONASS_FIRST_PRN && first_prn <= GLONASS_LAST_PRN ? CONSTELLATION_GLONASS : CONSTELLATION_GPS;
    }

    for (size_t i = 0; i < state.satellite_count; i++) {
        if (state.satellites[i].constellation == constellation) {
            state.satellites[i].used = false;
        }
    }
    for (size_t field = 3; field < 15; field++) {
        if (!*fields[field]) {
            continue;
        }
        nmea_satellite_t *satellite = find_satellite(constellation, (uint16_t)atoi(fields[field]), now_ms);
        if (satellite) {
            satellite->used = true;
            satellite->seen_ms = now_ms;
        }
    }
}

static void parse_gsv(const char *talker, char **fields, size_t count, uint32_t now_ms) {
    constellation_t constellation = talker_constellation(talker);
    for (size_t field = 4; field + 3 < count; field += 4) {
        if (!*fields[field]) {
            continue;
        }
        nmea_satellite_t *satellite = find_satellite(constellation, (uint16_t)atoi(fields[field]), now_ms);
        if (!satellite) {
            continue;
        }
        satellite->elevation = *fields[field + 1] ? (int16_t)atoi(fields[field + 1]) : -1;
        satellite->azimuth = *fields[field + 2] ? (int16_t)atoi(fields[field + 2]) : -1;
        satellite->snr = *fields[field + 3] ? (int16_t)atoi(fields[field + 3]) : -1;
        satellite->seen_ms = now_ms;
    }
}

bool nmea_feed(const char *sentence, size_t length, uint32_t now_ms) {
    if (length < 7 || length >= SENTENCE_MAX || sentence[0] != '$' || !checksum_valid(sentence, length)) {
        return false;
    }

    char body[SENTENCE_MAX];
    const char *star = memchr(sentence, '*', length);
    size_t body_length = (size_t)(star - sentence - 1);
    memcpy(body, sentence + 1, body_length);
    body[body_length] = '\0';

    char *fields[MAX_FIELDS];
    size_t count = split_fields(body, fields);
    const char *talker = fields[0];
    const char *type = fields[0] + 2;

    if (strcmp(type, "GGA") == 0) {
        parse_gga(fields, count);
    } else if (strcmp(type, "RMC") == 0) {
        parse_rmc(fields, count);
        return true;
    } else if (strcmp(type, "GSA") == 0) {
        parse_gsa(talker, fields, count, now_ms);
    } else if (strcmp(type, "GSV") == 0) {
        parse_gsv(talker, fields, count, now_ms);
    }
    return false;
}

void nmea_expire(uint32_t now_ms, uint32_t max_age_ms) {
    size_t kept = 0;
    for (size_t i = 0; i < state.satellite_count; i++) {
        if (now_ms - state.satellites[i].seen_ms <= max_age_ms) {
            state.satellites[kept++] = state.satellites[i];
        }
    }
    state.satellite_count = kept;
}

const nmea_state_t *nmea_state(void) {
    return &state;
}
