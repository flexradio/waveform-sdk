cmake_minimum_required(VERSION 3.16)
project(waveform-test C)
set(CMAKE_C_STANDARD 99)

find_package(LibWaveform)

add_executable(waveform-test main.c)
target_link_libraries(waveform-test PRIVATE LibWaveform::waveform)
