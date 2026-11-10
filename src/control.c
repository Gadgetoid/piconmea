#include <stdio.h>
#include <string.h>

#include "pico/stdio_usb.h"

#include "battery.h"
#include "control.h"
#include "wired.h"

#define LINE_MAX 128
#define COMMAND_PREFIX "$PPNM,"

static control_broadcast_t broadcast_handler;
static control_passthrough_t passthrough_handler;
static char line[LINE_MAX];
static size_t line_length;
static bool line_overflow;
static bool simulating;
static bool bluetooth_connected;

static void send_sentence(const char *body) {
    char sentence[64];
    uint8_t checksum = 0;
    for (const char *character = body; *character; character++) {
        checksum ^= (uint8_t)*character;
    }
    int length = snprintf(sentence, sizeof(sentence), "$%s*%02X\r\n", body, checksum);
    wired_write_usb(sentence, (size_t)length);
}

static void send_status(void) {
    char body[32];
    snprintf(body, sizeof(body), "PPNM,STATUS,%d,%d", simulating, bluetooth_connected);
    send_sentence(body);
}

static void send_battery(void) {
    battery_status_t battery = battery_read();
    char body[48];
    if (battery.present) {
        snprintf(body, sizeof(body), "PPNM,BATTERY,%d,%d,%d", (int)(battery.voltage * 1000.0f + 0.5f), battery.percent, battery.usb_power);
    } else {
        snprintf(body, sizeof(body), "PPNM,BATTERY,,,");
    }
    send_sentence(body);
}

static void handle_command(const char *command) {
    if (strcmp(command, "BATTERY") == 0) {
        send_battery();
        return;
    }
    if (strcmp(command, "SIM,1") == 0) {
        simulating = true;
    } else if (strcmp(command, "SIM,0") == 0) {
        simulating = false;
    } else if (strcmp(command, "STATUS") != 0) {
        return;
    }
    send_status();
}

static void handle_line(void) {
    line[line_length] = '\0';

    if (strncmp(line, COMMAND_PREFIX, strlen(COMMAND_PREFIX)) == 0) {
        char *checksum = strchr(line, '*');
        if (checksum) {
            *checksum = '\0';
        }
        handle_command(line + strlen(COMMAND_PREFIX));
        return;
    }

    line[line_length++] = '\r';
    line[line_length++] = '\n';
    if (simulating) {
        if (line[0] == '$') {
            broadcast_handler(line, line_length);
        }
    } else {
        passthrough_handler((const uint8_t *)line, line_length);
    }
}

void control_init(control_broadcast_t broadcast, control_passthrough_t passthrough) {
    broadcast_handler = broadcast;
    passthrough_handler = passthrough;
}

void control_input(const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        char character = (char)data[i];
        if (character == '\r') {
            continue;
        }
        if (character == '\n') {
            if (line_length && !line_overflow) {
                handle_line();
            }
            line_length = 0;
            line_overflow = false;
            continue;
        }
        if (line_length < LINE_MAX - 3) {
            line[line_length++] = character;
        } else {
            line_overflow = true;
        }
    }
}

void control_poll(void) {
    if (simulating && !stdio_usb_connected()) {
        simulating = false;
    }
}

bool control_simulating(void) {
    return simulating;
}

void control_set_bluetooth_connected(bool connected) {
    bluetooth_connected = connected;
    send_status();
}
