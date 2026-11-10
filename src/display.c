#include "display.h"

#include PICONMEA_BOARD_HEADER

#ifdef DISPLAY_SPI

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#define DISPLAY_BAUDRATE 24000000

#define ST7789_SWRESET 0x01
#define ST7789_SLPOUT 0x11
#define ST7789_INVON 0x21
#define ST7789_DISPON 0x29
#define ST7789_CASET 0x2A
#define ST7789_RASET 0x2B
#define ST7789_RAMWR 0x2C
#define ST7789_MADCTL 0x36
#define ST7789_COLMOD 0x3A
#define ST7789_RAMCTRL 0xB0
#define ST7789_PORCTRL 0xB2
#define ST7789_GCTRL 0xB7
#define ST7789_VCOMS 0xBB
#define ST7789_LCMCTRL 0xC0
#define ST7789_VDVVRHEN 0xC2
#define ST7789_VRHS 0xC3
#define ST7789_VDVS 0xC4
#define ST7789_FRCTRL2 0xC6
#define ST7789_PWCTRL1 0xD0
#define ST7789_GMCTRP1 0xE0
#define ST7789_GMCTRN1 0xE1

static uint16_t framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static int dma_channel = -1;
static bool backlight_on;

static void wait_for_transfer(void) {
    if (dma_channel >= 0) {
        dma_channel_wait_for_finish_blocking((uint)dma_channel);
    }
    while (spi_is_busy(DISPLAY_SPI)) {
    }
    gpio_put(DISPLAY_CS_PIN, 1);
    spi_set_format(DISPLAY_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

static void command(uint8_t reg, const uint8_t *data, size_t length) {
    gpio_put(DISPLAY_CS_PIN, 0);
    gpio_put(DISPLAY_DC_PIN, 0);
    spi_write_blocking(DISPLAY_SPI, &reg, 1);
    if (length) {
        gpio_put(DISPLAY_DC_PIN, 1);
        spi_write_blocking(DISPLAY_SPI, data, length);
    }
    gpio_put(DISPLAY_CS_PIN, 1);
}

static void command_bytes(uint8_t reg, const char *data, size_t length) {
    command(reg, (const uint8_t *)data, length);
}

bool display_init(void) {
    spi_init(DISPLAY_SPI, DISPLAY_BAUDRATE);
    gpio_set_function(DISPLAY_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(DISPLAY_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_init(DISPLAY_CS_PIN);
    gpio_set_dir(DISPLAY_CS_PIN, GPIO_OUT);
    gpio_put(DISPLAY_CS_PIN, 1);
    gpio_init(DISPLAY_DC_PIN);
    gpio_set_dir(DISPLAY_DC_PIN, GPIO_OUT);
    gpio_init(DISPLAY_BL_PIN);
    gpio_set_dir(DISPLAY_BL_PIN, GPIO_OUT);
    gpio_put(DISPLAY_BL_PIN, 0);

    command(ST7789_SWRESET, NULL, 0);
    sleep_ms(150);
    command_bytes(ST7789_COLMOD, "\x05", 1);
    command_bytes(ST7789_PORCTRL, "\x0c\x0c\x00\x33\x33", 5);
    command_bytes(ST7789_LCMCTRL, "\x2c", 1);
    command_bytes(ST7789_VDVVRHEN, "\x01", 1);
    command_bytes(ST7789_VRHS, "\x12", 1);
    command_bytes(ST7789_VDVS, "\x20", 1);
    command_bytes(ST7789_PWCTRL1, "\xa4\xa1", 2);
    command_bytes(ST7789_FRCTRL2, "\x0f", 1);
    command_bytes(ST7789_RAMCTRL, "\x00\xc0", 2);
    command_bytes(ST7789_GCTRL, "\x35", 1);
    command_bytes(ST7789_VCOMS, "\x1f", 1);
    command_bytes(ST7789_GMCTRP1, "\xd0\x08\x11\x08\x0c\x15\x39\x33\x50\x36\x13\x14\x29\x2d", 14);
    command_bytes(ST7789_GMCTRN1, "\xd0\x08\x10\x08\x06\x06\x39\x44\x51\x0b\x16\x14\x2f\x31", 14);
    command(ST7789_INVON, NULL, 0);
    command(ST7789_SLPOUT, NULL, 0);
    sleep_ms(120);
    command(ST7789_DISPON, NULL, 0);
    command_bytes(ST7789_CASET, "\x00\x00\x00\xef", 4);
    command_bytes(ST7789_RASET, "\x00\x00\x00\xef", 4);
    command_bytes(ST7789_MADCTL, "\x04", 1);

    dma_channel = dma_claim_unused_channel(true);
    return true;
}

uint16_t *display_framebuffer(void) {
    wait_for_transfer();
    return framebuffer;
}

void display_flush(void) {
    wait_for_transfer();

    uint8_t reg = ST7789_RAMWR;
    gpio_put(DISPLAY_CS_PIN, 0);
    gpio_put(DISPLAY_DC_PIN, 0);
    spi_write_blocking(DISPLAY_SPI, &reg, 1);
    gpio_put(DISPLAY_DC_PIN, 1);
    spi_set_format(DISPLAY_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    dma_channel_config config = dma_channel_get_default_config((uint)dma_channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_16);
    channel_config_set_dreq(&config, spi_get_dreq(DISPLAY_SPI, true));
    dma_channel_configure((uint)dma_channel, &config, &spi_get_hw(DISPLAY_SPI)->dr, framebuffer, DISPLAY_WIDTH * DISPLAY_HEIGHT, true);

    if (!backlight_on) {
        wait_for_transfer();
        gpio_put(DISPLAY_BL_PIN, 1);
        backlight_on = true;
    }
}

#else

bool display_init(void) {
    return false;
}

uint16_t *display_framebuffer(void) {
    return 0;
}

void display_flush(void) {
}

#endif
