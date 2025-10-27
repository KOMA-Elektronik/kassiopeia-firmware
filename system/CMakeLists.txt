add_library(system
    src/startup_sequence.c
)

target_link_libraries(system drivers pico_stdlib pico_time hardware_pwm hardware_gpio hardware_adc)

target_include_directories(system PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/.." 
    "${CMAKE_CURRENT_SOURCE_DIR}/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../drivers/inc"
)