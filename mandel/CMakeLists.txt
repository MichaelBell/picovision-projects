cmake_minimum_required(VERSION 3.12)

# Change your executable name to something creative!
set(NAME dv-mandel) # <-- Name your project/executable here!

# Add your source files
add_executable(${NAME}
    main.cpp # <-- Add source files here!
    mandelbrot.c
)

# Don't forget to link the libraries you need!
target_link_libraries(${NAME}
    pico_multicore
    hardware_uart
    hardware_interp
    pimoroni_i2c
    picovision
    pico_graphics
)

pico_enable_stdio_usb(${NAME} 1)

# create map/bin/hex file etc.
pico_add_extra_outputs(${NAME})

# Set up files for the release packages
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.uf2
    ${CMAKE_CURRENT_LIST_DIR}/README.md
    DESTINATION .
)

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_GENERATOR "ZIP" "TGZ")
include(CPack)
