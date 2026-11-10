#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "font8x8.h"
#include "gpsview.h"

#define BACKGROUND RGB565(0, 0, 0)
#define PANEL RGB565(24, 28, 36)
#define GRID RGB565(60, 70, 80)
#define TEXT RGB565(255, 255, 255)
#define DIM RGB565(140, 140, 140)
#define FIX_3D RGB565(60, 200, 90)
#define FIX_2D RGB565(230, 200, 40)
#define NO_FIX RGB565(230, 60, 50)
#define SIMULATING RGB565(230, 200, 40)
#define BLUETOOTH RGB565(70, 140, 255)
#define BATTERY_GOOD RGB565(60, 200, 90)
#define BATTERY_LOW RGB565(230, 200, 40)
#define BATTERY_CRITICAL RGB565(230, 60, 50)
#define BATTERY_LOW_PERCENT 50
#define BATTERY_CRITICAL_PERCENT 20

#define HEADER_HEIGHT 22
#define SKY_CENTRE_X 68
#define SKY_CENTRE_Y 94
#define SKY_RADIUS 60
#define SATELLITE_RADIUS 4
#define COLUMN_X 138
#define BARS_TOP 158
#define BARS_HEIGHT 46
#define BARS_LEFT 4
#define BARS_WIDTH 232
#define BAR_MAX_WIDTH 12
#define SNR_FULL_SCALE 50
#define LINE_HEIGHT 12

#ifdef PICONMEA_SPEED_KMH
#define SPEED_PER_KNOT 1.852f
#define SPEED_UNIT "km/h"
#else
#define SPEED_PER_KNOT 1.150779f
#define SPEED_UNIT "mph"
#endif

static const uint16_t constellation_colours[CONSTELLATION_COUNT] = {
    RGB565(60, 200, 90),
    RGB565(240, 140, 40),
    RGB565(70, 140, 255),
    RGB565(220, 80, 200),
    RGB565(140, 140, 140),
};

static const char *constellation_names[CONSTELLATION_COUNT] = {"GPS", "GLO", "GAL", "BDS", "OTH"};

static uint16_t *framebuffer;
static bool available;

static void set_pixel(int x, int y, uint16_t colour) {
    if (x >= 0 && x < DISPLAY_WIDTH && y >= 0 && y < DISPLAY_HEIGHT) {
        framebuffer[y * DISPLAY_WIDTH + x] = colour;
    }
}

static void fill_rect(int x, int y, int width, int height, uint16_t colour) {
    for (int row = y; row < y + height; row++) {
        for (int column = x; column < x + width; column++) {
            set_pixel(column, row, colour);
        }
    }
}

static void draw_rect(int x, int y, int width, int height, uint16_t colour) {
    fill_rect(x, y, width, 1, colour);
    fill_rect(x, y + height - 1, width, 1, colour);
    fill_rect(x, y, 1, height, colour);
    fill_rect(x + width - 1, y, 1, height, colour);
}

static void draw_line(int x0, int y0, int x1, int y1, uint16_t colour) {
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int step_x = x0 < x1 ? 1 : -1;
    int step_y = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        set_pixel(x0, y0, colour);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x0 += step_x;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += step_y;
        }
    }
}

static void draw_circle(int centre_x, int centre_y, int radius, uint16_t colour) {
    int x = radius;
    int y = 0;
    int error = 1 - radius;
    while (x >= y) {
        set_pixel(centre_x + x, centre_y + y, colour);
        set_pixel(centre_x + y, centre_y + x, colour);
        set_pixel(centre_x - y, centre_y + x, colour);
        set_pixel(centre_x - x, centre_y + y, colour);
        set_pixel(centre_x - x, centre_y - y, colour);
        set_pixel(centre_x - y, centre_y - x, colour);
        set_pixel(centre_x + y, centre_y - x, colour);
        set_pixel(centre_x + x, centre_y - y, colour);
        y++;
        if (error < 0) {
            error += 2 * y + 1;
        } else {
            x--;
            error += 2 * (y - x) + 1;
        }
    }
}

static void fill_circle(int centre_x, int centre_y, int radius, uint16_t colour) {
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x * x + y * y <= radius * radius) {
                set_pixel(centre_x + x, centre_y + y, colour);
            }
        }
    }
}

static int text_width(const char *text, int scale) {
    return (int)strlen(text) * 8 * scale;
}

static void draw_text(int x, int y, const char *text, uint16_t colour, int scale) {
    for (; *text; text++, x += 8 * scale) {
        int index = *text - FONT8X8_FIRST;
        if (index < 0 || index >= FONT8X8_GLYPHS) {
            continue;
        }
        for (int row = 0; row < 8; row++) {
            uint8_t bits = font8x8[index][row];
            for (int column = 0; column < 8; column++) {
                if (bits & (1 << column)) {
                    fill_rect(x + column * scale, y + row * scale, scale, scale, colour);
                }
            }
        }
    }
}

static void draw_text_centred(int y, const char *text, uint16_t colour, int scale) {
    draw_text((DISPLAY_WIDTH - text_width(text, scale)) / 2, y, text, colour, scale);
}

static void draw_header(const nmea_state_t *state, bool simulating, bool bluetooth_connected) {
    fill_rect(0, 0, DISPLAY_WIDTH, HEADER_HEIGHT, PANEL);

    if (state->fix_mode == 3 || (state->fix_quality && state->fix_mode == 0)) {
        draw_text(4, 3, "3D FIX", FIX_3D, 2);
    } else if (state->fix_mode == 2) {
        draw_text(4, 3, "2D FIX", FIX_2D, 2);
    } else {
        draw_text(4, 3, "NO FIX", NO_FIX, 2);
    }

    if (simulating) {
        draw_text(108, 7, "SIM", SIMULATING, 1);
    }
    draw_text(140, 7, "BT", bluetooth_connected ? BLUETOOTH : GRID, 1);

    char time[16];
    if (state->time_valid) {
        snprintf(time, sizeof(time), "%02u:%02u:%02u", state->hour, state->minute, state->second);
    } else {
        snprintf(time, sizeof(time), "--:--:--");
    }
    draw_text(DISPLAY_WIDTH - 4 - text_width(time, 1), 7, time, TEXT, 1);
}

static void draw_sky(const nmea_state_t *state) {
    draw_circle(SKY_CENTRE_X, SKY_CENTRE_Y, SKY_RADIUS, GRID);
    draw_circle(SKY_CENTRE_X, SKY_CENTRE_Y, SKY_RADIUS * 2 / 3, GRID);
    draw_circle(SKY_CENTRE_X, SKY_CENTRE_Y, SKY_RADIUS / 3, GRID);
    draw_line(SKY_CENTRE_X - SKY_RADIUS, SKY_CENTRE_Y, SKY_CENTRE_X + SKY_RADIUS, SKY_CENTRE_Y, GRID);
    draw_line(SKY_CENTRE_X, SKY_CENTRE_Y - SKY_RADIUS, SKY_CENTRE_X, SKY_CENTRE_Y + SKY_RADIUS, GRID);
    draw_text(SKY_CENTRE_X - 3, SKY_CENTRE_Y - SKY_RADIUS - 10, "N", DIM, 1);

    for (size_t i = 0; i < state->satellite_count; i++) {
        const nmea_satellite_t *satellite = &state->satellites[i];
        if (satellite->elevation < 0 || satellite->azimuth < 0) {
            continue;
        }
        float distance = SKY_RADIUS * (90.0f - (float)satellite->elevation) / 90.0f;
        float angle = (float)satellite->azimuth * (float)M_PI / 180.0f;
        int x = SKY_CENTRE_X + (int)lroundf(distance * sinf(angle));
        int y = SKY_CENTRE_Y - (int)lroundf(distance * cosf(angle));
        uint16_t colour = constellation_colours[satellite->constellation];
        if (satellite->used) {
            fill_circle(x, y, SATELLITE_RADIUS, colour);
        } else {
            draw_circle(x, y, SATELLITE_RADIUS, colour);
        }
    }
}

static void draw_summary(const nmea_state_t *state) {
    unsigned used[CONSTELLATION_COUNT] = {0};
    unsigned visible[CONSTELLATION_COUNT] = {0};
    unsigned total_used = 0;
    for (size_t i = 0; i < state->satellite_count; i++) {
        const nmea_satellite_t *satellite = &state->satellites[i];
        visible[satellite->constellation]++;
        if (satellite->used) {
            used[satellite->constellation]++;
            total_used++;
        }
    }

    char line[32];
    draw_text(COLUMN_X, 26, "SATS", DIM, 1);
    snprintf(line, sizeof(line), "%u/%u", total_used, (unsigned)state->satellite_count);
    draw_text(COLUMN_X, 38, line, TEXT, 2);

    int y = 60;
    for (int constellation = 0; constellation < CONSTELLATION_COUNT; constellation++) {
        if (!visible[constellation]) {
            continue;
        }
        snprintf(line, sizeof(line), "%s %2u/%-2u", constellation_names[constellation], used[constellation], visible[constellation]);
        draw_text(COLUMN_X, y, line, constellation_colours[constellation], 1);
        y += LINE_HEIGHT;
    }

    snprintf(line, sizeof(line), "HDOP %.1f", (double)state->hdop);
    draw_text(COLUMN_X, 112, line, DIM, 1);
    snprintf(line, sizeof(line), "PDOP %.1f", (double)state->pdop);
    draw_text(COLUMN_X, 124, line, DIM, 1);
    snprintf(line, sizeof(line), "CRS %3.0f", (double)state->course);
    draw_text(COLUMN_X, 136, line, DIM, 1);
}

static void draw_battery(const battery_status_t *battery) {
    if (!battery->present) {
        return;
    }
    char line[24];
    if (battery->usb_power) {
        snprintf(line, sizeof(line), "USB %.2fV", (double)battery->voltage);
        draw_text(COLUMN_X, 148, line, BLUETOOTH, 1);
        return;
    }
    uint16_t colour = battery->percent < BATTERY_CRITICAL_PERCENT ? BATTERY_CRITICAL : battery->percent < BATTERY_LOW_PERCENT ? BATTERY_LOW : BATTERY_GOOD;
    snprintf(line, sizeof(line), "BAT %d%% %.1fV", battery->percent, (double)battery->voltage);
    draw_text(COLUMN_X, 148, line, colour, 1);
}

static int compare_satellites(const void *a, const void *b) {
    const nmea_satellite_t *left = a;
    const nmea_satellite_t *right = b;
    if (left->constellation != right->constellation) {
        return (int)left->constellation - (int)right->constellation;
    }
    return (int)left->prn - (int)right->prn;
}

static void draw_signal_bars(const nmea_state_t *state) {
    static nmea_satellite_t sorted[NMEA_MAX_SATELLITES];
    size_t count = 0;
    for (size_t i = 0; i < state->satellite_count; i++) {
        if (state->satellites[i].snr >= 0) {
            sorted[count++] = state->satellites[i];
        }
    }
    if (!count) {
        return;
    }
    qsort(sorted, count, sizeof(sorted[0]), compare_satellites);

    int pitch = BARS_WIDTH / (int)count;
    if (pitch > BAR_MAX_WIDTH) {
        pitch = BAR_MAX_WIDTH;
    }
    int x = BARS_LEFT + (BARS_WIDTH - pitch * (int)count) / 2;
    fill_rect(BARS_LEFT, BARS_TOP + BARS_HEIGHT, BARS_WIDTH, 1, GRID);
    for (size_t i = 0; i < count; i++, x += pitch) {
        int snr = sorted[i].snr > SNR_FULL_SCALE ? SNR_FULL_SCALE : sorted[i].snr;
        int height = snr * BARS_HEIGHT / SNR_FULL_SCALE;
        if (height < 1) {
            height = 1;
        }
        uint16_t colour = constellation_colours[sorted[i].constellation];
        if (sorted[i].used) {
            fill_rect(x, BARS_TOP + BARS_HEIGHT - height, pitch - 1, height, colour);
        } else {
            draw_rect(x, BARS_TOP + BARS_HEIGHT - height, pitch - 1, height, colour);
        }
    }
}

static void draw_position(const nmea_state_t *state) {
    char line[40];
    if (state->position_valid || state->fix_quality) {
        snprintf(line, sizeof(line), "%.5f%c %.5f%c", fabs(state->latitude), state->latitude < 0 ? 'S' : 'N', fabs(state->longitude), state->longitude < 0 ? 'W' : 'E');
        draw_text_centred(210, line, TEXT, 1);
        snprintf(line, sizeof(line), "ALT %.0fm  SPD %.1f" SPEED_UNIT, (double)state->altitude, (double)(state->speed_knots * SPEED_PER_KNOT));
        draw_text_centred(224, line, DIM, 1);
    } else {
        draw_text_centred(216, "Waiting for fix", DIM, 1);
    }
}

bool gpsview_init(void) {
    available = display_init();
    return available;
}

void gpsview_render(const nmea_state_t *state, const battery_status_t *battery, bool simulating, bool bluetooth_connected) {
    if (!available) {
        return;
    }
    framebuffer = display_framebuffer();
    fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, BACKGROUND);
    draw_header(state, simulating, bluetooth_connected);
    draw_sky(state);
    draw_summary(state);
    draw_battery(battery);
    draw_signal_bars(state);
    draw_position(state);
    display_flush();
}
