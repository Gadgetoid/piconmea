#ifndef _PICONMEA_DISPLAY_H
#define _PICONMEA_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 240

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

bool display_init(void);
uint16_t *display_framebuffer(void);
void display_flush(void);

#endif
