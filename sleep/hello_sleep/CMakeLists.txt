add_executable(hello_sleep
        hello_sleep.c
        )

target_link_libraries(hello_sleep pico_stdlib hardware_sleep)

# create map/bin/hex file etc.
pico_add_extra_outputs(hello_sleep)