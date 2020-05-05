cmake_minimum_required(VERSION 3.16)
project(waveform_sdk C)

set(CMAKE_C_STANDARD 99)

set(WAVEFORM_SRCS
   src/utils.c
)

add_subdirectory(tests)

add_library(waveform ${WAVEFORM_SRCS})