#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "btstack.h"
#include "btstack_ring_buffer.h"
#include "hci_dump_embedded_stdout.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "control.h"
#include "gps.h"
#include "gpsview.h"
#include "nmea.h"
#include "wired.h"

#define DEVICE_NAME "PicoGPS 00:00:00:00:00:00"
#define SERVICE_NAME "Serial Port"
#define PIN_CODE "0000"
#define CLASS_OF_DEVICE 0x001F00
#define RFCOMM_SERVER_CHANNEL 1
#define GPS_POLL_PERIOD_MS 100
#define NMEA_BUFFER_SIZE 2048
#define RFCOMM_SEND_MAX 512
#define VIEW_REFRESH_MS 1000
#define SATELLITE_MAX_AGE_MS 5000

#ifdef PICONMEA_HCI_DUMP
#define log_status(...) printf(__VA_ARGS__)
#else
#define log_status(...)
#endif

static uint8_t spp_service_buffer[150];
static btstack_packet_callback_registration_t hci_event_callback_registration;
static btstack_timer_source_t gps_timer;

static uint16_t rfcomm_cid;
static uint16_t rfcomm_max_frame_size;
static btstack_ring_buffer_t nmea_ring_buffer;
static uint8_t nmea_storage[NMEA_BUFFER_SIZE];
static uint8_t send_buffer[RFCOMM_SEND_MAX];
static bool view_dirty = true;
static uint32_t view_rendered_ms;

static void set_led(bool on) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
}

static void broadcast_sentence(const char *sentence, size_t length) {
    if (nmea_feed(sentence, length, to_ms_since_boot(get_absolute_time()))) {
        view_dirty = true;
    }
    wired_write_uart(sentence, length);

    if (!rfcomm_cid) {
        return;
    }
    if (btstack_ring_buffer_bytes_free(&nmea_ring_buffer) < length) {
        return;
    }
    btstack_ring_buffer_write(&nmea_ring_buffer, (uint8_t *)sentence, length);
    rfcomm_request_can_send_now_event(rfcomm_cid);
}

static void handle_gps_sentence(const char *sentence, size_t length) {
    if (control_simulating()) {
        return;
    }
    wired_write_usb(sentence, length);
    broadcast_sentence(sentence, length);
}

static void update_view(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (!view_dirty && now - view_rendered_ms < VIEW_REFRESH_MS) {
        return;
    }
    nmea_expire(now, SATELLITE_MAX_AGE_MS);
    gpsview_render(nmea_state(), control_simulating(), rfcomm_cid != 0);
    view_dirty = false;
    view_rendered_ms = now;
}

static void gps_timer_handler(btstack_timer_source_t *timer) {
    gps_poll();
    wired_poll();
    control_poll();
    update_view();
    btstack_run_loop_set_timer(timer, GPS_POLL_PERIOD_MS);
    btstack_run_loop_add_timer(timer);
}

static void send_pending_nmea(void) {
    uint32_t available = btstack_ring_buffer_bytes_available(&nmea_ring_buffer);
    if (!rfcomm_cid || !available) {
        return;
    }

    uint32_t to_send = btstack_min(available, btstack_min(rfcomm_max_frame_size, sizeof(send_buffer)));
    uint32_t bytes_read = 0;
    btstack_ring_buffer_read(&nmea_ring_buffer, send_buffer, to_send, &bytes_read);
    rfcomm_send(rfcomm_cid, send_buffer, (uint16_t)bytes_read);

    if (btstack_ring_buffer_bytes_available(&nmea_ring_buffer)) {
        rfcomm_request_can_send_now_event(rfcomm_cid);
    }
}

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    bd_addr_t event_addr;

    if (packet_type == RFCOMM_DATA_PACKET) {
        gps_write(packet, size);
        return;
    }

    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                gap_local_bd_addr(event_addr);
                log_status("Ready: %s, PIN %s\n", bd_addr_to_str(event_addr), PIN_CODE);
            }
            break;

        case HCI_EVENT_PIN_CODE_REQUEST:
            hci_event_pin_code_request_get_bd_addr(packet, event_addr);
            log_status("PIN request from %s\n", bd_addr_to_str(event_addr));
            gap_pin_code_response(event_addr, PIN_CODE);
            break;

        case HCI_EVENT_USER_CONFIRMATION_REQUEST:
            log_status("SSP confirmation %06" PRIu32 ", auto accept\n", little_endian_read_32(packet, 8));
            break;

        case GAP_EVENT_PAIRING_COMPLETE:
            log_status("Pairing complete, status 0x%02x\n", gap_event_pairing_complete_get_status(packet));
            break;

        case RFCOMM_EVENT_INCOMING_CONNECTION:
            rfcomm_event_incoming_connection_get_bd_addr(packet, event_addr);
            log_status("RFCOMM connection from %s\n", bd_addr_to_str(event_addr));
            rfcomm_accept_connection(rfcomm_event_incoming_connection_get_rfcomm_cid(packet));
            break;

        case RFCOMM_EVENT_CHANNEL_OPENED:
            if (rfcomm_event_channel_opened_get_status(packet)) {
                log_status("RFCOMM open failed, status 0x%02x\n", rfcomm_event_channel_opened_get_status(packet));
                break;
            }
            rfcomm_cid = rfcomm_event_channel_opened_get_rfcomm_cid(packet);
            rfcomm_max_frame_size = rfcomm_event_channel_opened_get_max_frame_size(packet);
            btstack_ring_buffer_reset(&nmea_ring_buffer);
            log_status("RFCOMM open, cid %u, max frame %u\n", rfcomm_cid, rfcomm_max_frame_size);
            set_led(true);
            control_set_bluetooth_connected(true);
            view_dirty = true;
            break;

        case RFCOMM_EVENT_CAN_SEND_NOW:
            send_pending_nmea();
            break;

        case RFCOMM_EVENT_CHANNEL_CLOSED:
            log_status("RFCOMM closed\n");
            rfcomm_cid = 0;
            set_led(false);
            control_set_bluetooth_connected(false);
            view_dirty = true;
            break;

        default:
            break;
    }
}

static void bluetooth_setup(void) {
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    l2cap_init();
    rfcomm_init();
    rfcomm_register_service(packet_handler, RFCOMM_SERVER_CHANNEL, 0xffff);

    sdp_init();
    memset(spp_service_buffer, 0, sizeof(spp_service_buffer));
    spp_create_sdp_record(spp_service_buffer, sdp_create_service_record_handle(), RFCOMM_SERVER_CHANNEL, SERVICE_NAME);
    btstack_assert(de_get_len(spp_service_buffer) <= sizeof(spp_service_buffer));
    sdp_register_service(spp_service_buffer);

    gap_set_local_name(DEVICE_NAME);
    gap_set_class_of_device(CLASS_OF_DEVICE);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_set_allow_role_switch(true);
    gap_discoverable_control(1);
    gap_connectable_control(1);
}

int main(void) {
    stdio_init_all();

#ifdef PICONMEA_HCI_DUMP
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    hci_dump_init(hci_dump_embedded_stdout_get_instance());
#endif

    if (cyw43_arch_init()) {
        return -1;
    }

    gpsview_init();
    btstack_ring_buffer_init(&nmea_ring_buffer, nmea_storage, sizeof(nmea_storage));
    gps_init(handle_gps_sentence);
    control_init(broadcast_sentence, gps_write);
    wired_init(gps_write, control_input);

    bluetooth_setup();

    gps_timer.process = &gps_timer_handler;
    btstack_run_loop_set_timer(&gps_timer, GPS_POLL_PERIOD_MS);
    btstack_run_loop_add_timer(&gps_timer);

    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();
    return 0;
}
