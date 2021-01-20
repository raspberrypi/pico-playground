add_executable(hello_dormant
        hello_dormant.c
        )

target_link_libraries(hello_dormant pico_stdlib hardware_sleep)

# create map/bin/hex file etc.
pico_add_extra_outputs(hello_dormant)