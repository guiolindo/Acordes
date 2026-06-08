cmake_minimum_required(VERSION 3.22.1)
project("acordes")

find_package(oboe REQUIRED CONFIG)

add_library(acordes SHARED
    jni_bridge.cpp
    chord_engine.cpp
    kissfft/kiss_fft.c
)

target_include_directories(acordes PRIVATE
    kissfft/
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(acordes PRIVATE
    -O3
    -ffast-math
    -fno-exceptions
)

target_link_libraries(acordes
    oboe::oboe
    android
    log
)
