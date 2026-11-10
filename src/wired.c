#include <stdbool.h>
#include <string.h>

#include "hardware/irq.h"
#include "hardware/uart.h"
#include "pico/stdio/driver.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include PICONMEA_BOARD_HEADER
#include "wired.h"

#define WIRED_UART_BAUDRATE 9600
#define TX_BUFFER_SIZE 1024
#define RX_BUFFER_SIZE 256
#define INPUT_CHUNK 64

static wired_input_handler_t uart_input_handler;
static wired_input_handler_t usb_input_handler;

static uint8_t tx_buffer[TX_BUFFER_SIZE];
static volatile uint16_t tx_head;
static volatile uint16_t tx_tail;

static uint8_t rx_buffer[RX_BUFFER_SIZE];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;

static void uart_fill_tx_fifo(void) {
    while (tx_tail != tx_head && uart_is_writable(WIRED_UART)) {
        uart_get_hw(WIRED_UART)->dr = tx_buffer[tx_tail];
        tx_tail = (tx_tail + 1) % TX_BUFFER_SIZE;
    }
    if (tx_tail == tx_head) {
        hw_clear_bits(&uart_get_hw(WIRED_UART)->imsc, UART_UARTIMSC_TXIM_BITS);
    } else {
        hw_set_bits(&uart_get_hw(WIRED_UART)->imsc, UART_UARTIMSC_TXIM_BITS);
    }
}

static void uart_irq_handler(void) {
    while (uart_is_readable(WIRED_UART)) {
        uint8_t byte = (uint8_t)uart_get_hw(WIRED_UART)->dr;
        uint16_t next = (rx_head + 1) % RX_BUFFER_SIZE;
        if (next != rx_tail) {
            rx_buffer[rx_head] = byte;
            rx_head = next;
        }
    }
    uart_fill_tx_fifo();
}

void wired_init(wired_input_handler_t uart_handler, wired_input_handler_t usb_handler) {
    uart_input_handler = uart_handler;
    usb_input_handler = usb_handler;

    uart_init(WIRED_UART, WIRED_UART_BAUDRATE);
    gpio_set_function(WIRED_UART_TX_PIN, UART_FUNCSEL_NUM(WIRED_UART, WIRED_UART_TX_PIN));
    gpio_set_function(WIRED_UART_RX_PIN, UART_FUNCSEL_NUM(WIRED_UART, WIRED_UART_RX_PIN));
    uart_set_hw_flow(WIRED_UART, false, false);
    uart_set_format(WIRED_UART, 8, 1, UART_PARITY_NONE);

    irq_set_exclusive_handler(UART_IRQ_NUM(WIRED_UART), uart_irq_handler);
    irq_set_enabled(UART_IRQ_NUM(WIRED_UART), true);
    uart_set_irqs_enabled(WIRED_UART, true, false);
}

void wired_write_uart(const char *sentence, size_t length) {
    uint16_t used = (tx_head - tx_tail + TX_BUFFER_SIZE) % TX_BUFFER_SIZE;
    if (TX_BUFFER_SIZE - 1 - used < length) {
        return;
    }

    uint16_t head = tx_head;
    for (size_t i = 0; i < length; i++) {
        tx_buffer[head] = (uint8_t)sentence[i];
        head = (head + 1) % TX_BUFFER_SIZE;
    }

    irq_set_enabled(UART_IRQ_NUM(WIRED_UART), false);
    tx_head = head;
    uart_fill_tx_fifo();
    irq_set_enabled(UART_IRQ_NUM(WIRED_UART), true);
}

void wired_write_usb(const char *sentence, size_t length) {
#ifndef PICONMEA_HCI_DUMP
    stdio_usb.out_chars(sentence, (int)length);
#endif
}

void wired_poll(void) {
    uint8_t chunk[INPUT_CHUNK];
    size_t length = 0;

    while (rx_tail != rx_head && length < sizeof(chunk)) {
        chunk[length++] = rx_buffer[rx_tail];
        rx_tail = (rx_tail + 1) % RX_BUFFER_SIZE;
    }
    if (length) {
        uart_input_handler(chunk, length);
        length = 0;
    }

#ifndef PICONMEA_HCI_DUMP
    while (length < sizeof(chunk)) {
        int character = getchar_timeout_us(0);
        if (character == PICO_ERROR_TIMEOUT) {
            break;
        }
        chunk[length++] = (uint8_t)character;
    }
#endif

    if (length) {
        usb_input_handler(chunk, length);
    }
}
