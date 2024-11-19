add_executable(hello_dormant_gpio
        hello_dormant_gpio.c
        )
target_link_libraries(hello_dormant_gpio
        pico_stdlib
        hardware_sleep)
pico_add_extra_outputs(hello_dormant_gpio)

add_executable(hello_dormant_aon
        hello_dormant_aon.c
        )
target_link_libraries(hello_dormant_aon
        pico_stdlib
        hardware_sleep
        )
pico_add_extra_outputs(hello_dormant_aon)
