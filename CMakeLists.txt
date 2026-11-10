cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(piconmea C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

set(PICONMEA_BOARD_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/src/boards/${PICO_BOARD}.h)
if(NOT EXISTS ${PICONMEA_BOARD_HEADER})
    message(FATAL_ERROR "No pin configuration for board '${PICO_BOARD}', add src/boards/${PICO_BOARD}.h")
endif()

add_executable(piconmea
    src/main.c
    src/battery.c
    src/control.c
    src/display.c
    src/font8x8.c
    src/gps.c
    src/gpsview.c
    src/nmea.c
    src/wired.c
)

target_include_directories(piconmea PRIVATE src)

target_compile_definitions(piconmea PRIVATE
    CYW43_LWIP=0
    PICONMEA_BOARD_HEADER="boards/${PICO_BOARD}.h"
)

option(PICONMEA_HCI_DUMP "Dump HCI packets to USB stdio" OFF)
if(PICONMEA_HCI_DUMP)
    target_compile_definitions(piconmea PRIVATE PICONMEA_HCI_DUMP=1)
endif()

target_link_libraries(piconmea
    pico_stdlib
    hardware_adc
    hardware_dma
    hardware_i2c
    hardware_spi
    hardware_uart
    pico_btstack_classic
    pico_btstack_cyw43
    pico_cyw43_arch_none
)

pico_enable_stdio_usb(piconmea 1)
pico_enable_stdio_uart(piconmea 0)

pico_add_extra_outputs(piconmea)
