#ifndef _PICONMEA_CONTROL_H
#define _PICONMEA_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*control_broadcast_t)(const char *sentence, size_t length);
typedef void (*control_passthrough_t)(const uint8_t *data, size_t length);

void control_init(control_broadcast_t broadcast, control_passthrough_t passthrough);
void control_input(const uint8_t *data, size_t length);
void control_poll(void);
bool control_simulating(void);
void control_set_bluetooth_connected(bool connected);

#endif
