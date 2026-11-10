#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battery.h"
#include "gpsview.h"
#include "nmea.h"

void display_mock_set_output(const char *path);

int main(int argc, char **argv) {
    if (argc != 9) {
        fprintf(stderr, "usage: %s <nmea file> <output.ppm> <simulating> <bluetooth> <battery present> <voltage> <percent> <usb power>\n", argv[0]);
        return 1;
    }

    FILE *input = fopen(argv[1], "r");
    if (!input) {
        perror(argv[1]);
        return 1;
    }

    char line[256];
    while (fgets(line, sizeof(line), input)) {
        nmea_feed(line, strcspn(line, "\r\n"), 0);
    }
    fclose(input);

    battery_status_t battery = {
        .present = atoi(argv[5]) != 0,
        .voltage = (float)atof(argv[6]),
        .percent = atoi(argv[7]),
        .usb_power = atoi(argv[8]) != 0,
    };

    display_mock_set_output(argv[2]);
    gpsview_init();
    gpsview_render(nmea_state(), &battery, atoi(argv[3]) != 0, atoi(argv[4]) != 0);
    return 0;
}
