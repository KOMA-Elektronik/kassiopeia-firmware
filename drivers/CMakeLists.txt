add_library(drivers
    src/mux_4051.c
    src/adc.c
    src/pulse.c
    src/pwm.c
    src/midi.c
)

target_link_libraries(drivers pico_stdlib pico_time hardware_pwm hardware_gpio hardware_adc)

target_include_directories(drivers PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/.." 
    "${CMAKE_CURRENT_SOURCE_DIR}/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../third-party/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../system/inc"
)