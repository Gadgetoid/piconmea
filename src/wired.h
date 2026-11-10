#ifndef _PICONMEA_WIRED_H
#define _PICONMEA_WIRED_H

#include <stddef.h>
#include <stdint.h>

typedef void (*wired_input_handler_t)(const uint8_t *data, size_t length);

void wired_init(wired_input_handler_t uart_handler, wired_input_handler_t usb_handler);
void wired_write_uart(const char *sentence, size_t length);
void wired_write_usb(const char *sentence, size_t length);
void wired_poll(void);

#endif
