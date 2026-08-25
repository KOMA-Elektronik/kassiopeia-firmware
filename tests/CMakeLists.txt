cmake_minimum_required(VERSION 3.13)

project(kassiopeia_host_tests C)

enable_testing()

add_executable(test_power_monitor
    test_power_monitor.c
    ../system/src/power_monitor.c
)

target_include_directories(test_power_monitor PRIVATE ../system/inc)
target_compile_options(test_power_monitor PRIVATE -Wall -Wextra -Werror)

add_test(NAME power_monitor COMMAND test_power_monitor)
