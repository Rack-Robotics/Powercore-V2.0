cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(main)

pico_sdk_init()

add_executable(main)

pico_generate_pio_header(main ${CMAKE_CURRENT_LIST_DIR}/CLK.pio)
pico_generate_pio_header(main ${CMAKE_CURRENT_LIST_DIR}/TIMING.pio)

target_sources( main 
                PRIVATE main.c 
                CC_Charger.c
                pulse_generator.c)

# pull in common dependencies
target_link_libraries(  main 
                        pico_stdlib 
                        hardware_pio
                        hardware_adc
                        hardware_pwm
                        pico_time)

pico_enable_stdio_uart(main 1)
pico_enable_stdio_usb(main 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(main)