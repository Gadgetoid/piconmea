#include <stdbool.h>

#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include PICONMEA_BOARD_HEADER
#include "gps.h"

#define GPS_I2C_ADDRESS 0x10
#define GPS_I2C_BAUDRATE 400000
#define GPS_READ_CHUNK 255
#define GPS_SENTENCE_MAX 128

static gps_sentence_handler_t sentence_handler;
static char sentence[GPS_SENTENCE_MAX];
static size_t sentence_length;
static bool in_sentence;
static bool seen_carriage_return;

void gps_init(gps_sentence_handler_t handler) {
    sentence_handler = handler;

    i2c_init(GPS_I2C, GPS_I2C_BAUDRATE);
    gpio_set_function(GPS_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(GPS_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(GPS_SDA_PIN);
    gpio_pull_up(GPS_SCL_PIN);
}

static void gps_parse_byte(uint8_t byte) {
    if (byte == '$') {
        in_sentence = true;
        seen_carriage_return = false;
        sentence_length = 0;
        sentence[sentence_length++] = (char)byte;
        return;
    }

    if (!in_sentence) {
        return;
    }

    if (byte == '\r') {
        seen_carriage_return = true;
        return;
    }

    if (byte == '\n') {
        if (seen_carriage_return) {
            sentence[sentence_length++] = '\r';
            sentence[sentence_length++] = '\n';
            sentence_handler(sentence, sentence_length);
            in_sentence = false;
        }
        return;
    }

    if (seen_carriage_return || byte < 0x20 || byte > 0x7e || sentence_length >= GPS_SENTENCE_MAX - 2) {
        in_sentence = false;
        return;
    }

    sentence[sentence_length++] = (char)byte;
}

void gps_poll(void) {
    uint8_t buffer[GPS_READ_CHUNK];
    int result = i2c_read_timeout_us(GPS_I2C, GPS_I2C_ADDRESS, buffer, sizeof(buffer), false, 50000);
    if (result <= 0) {
        return;
    }
    for (int i = 0; i < result; i++) {
        gps_parse_byte(buffer[i]);
    }
}

void gps_write(const uint8_t *data, size_t length) {
    i2c_write_timeout_us(GPS_I2C, GPS_I2C_ADDRESS, data, length, false, 50000);
}
