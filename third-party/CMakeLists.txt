add_library(third-party
    src/nano_midi.c
)

target_include_directories(third-party PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/inc")