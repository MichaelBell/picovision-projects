cmake_minimum_required(VERSION 3.12)

include(pimoroni_pico_import.cmake)
include(pico_sdk_import.cmake)

# Gooey boilerplate
project(picovision-projects C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

include(picovision_import.cmake)

# Include required libraries
# This assumes `pimoroni-pico` is stored alongside your project
include(common/pimoroni_i2c)
include(libraries/pico_graphics/pico_graphics)
include(libraries/pico_vector/pico_vector)

add_subdirectory(gol)
add_subdirectory(bubbles)
add_subdirectory(edid-display)
add_subdirectory(mandel)