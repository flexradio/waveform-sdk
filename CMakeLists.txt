cmake_minimum_required(VERSION 3.16)
project(waveform_sdk)

set(CMAKE_C_STANDARD 99)

include(FetchContent)

add_subdirectory(libpwq)
add_subdirectory(src)
add_subdirectory(tests)

add_executable(waveform-test main.c)
target_link_libraries(waveform-test waveform m)
