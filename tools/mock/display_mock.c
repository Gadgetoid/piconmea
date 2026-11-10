#include <stdio.h>

#include "display.h"

static uint16_t framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static const char *output_path;

void display_mock_set_output(const char *path) {
    output_path = path;
}

bool display_init(void) {
    return true;
}

uint16_t *display_framebuffer(void) {
    return framebuffer;
}

void display_flush(void) {
    FILE *output = fopen(output_path, "wb");
    fprintf(output, "P6\n%d %d\n255\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
        uint16_t pixel = framebuffer[i];
        uint8_t rgb[3] = {
            (uint8_t)(((pixel >> 11) & 0x1F) * 255 / 31),
            (uint8_t)(((pixel >> 5) & 0x3F) * 255 / 63),
            (uint8_t)((pixel & 0x1F) * 255 / 31),
        };
        fwrite(rgb, 1, 3, output);
    }
    fclose(output);
}
