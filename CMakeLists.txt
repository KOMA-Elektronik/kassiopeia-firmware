cmake_minimum_required(VERSION 3.13)

include(pico-sdk/pico_sdk_init.cmake)

project(kassiopeia)

pico_sdk_init()

add_executable(kassiopeia main.c)

add_subdirectory(system)
add_subdirectory(drivers)
add_subdirectory(third-party)

target_compile_definitions(kassiopeia PRIVATE
    "PICO_XOSC_STARTUP_DELAY_MULTIPLIER=128"
)

# Reduce the SDK's per-chunk USB no-progress timeout for bench diagnostics. This
# does not bound a complete printf, mutex acquisition, or final flush, and a line
# may be truncated if the host stops reading.
target_compile_definitions(pico_stdio_usb INTERFACE
    "PICO_STDIO_USB_STDOUT_TIMEOUT_US=2000"
)

# pull in common dependencies
target_link_libraries(kassiopeia pico_stdlib hardware_pwm hardware_adc hardware_dma hardware_sync hardware_flash system drivers third-party)

pico_enable_stdio_usb(kassiopeia 1)
pico_enable_stdio_uart(kassiopeia 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(kassiopeia)
