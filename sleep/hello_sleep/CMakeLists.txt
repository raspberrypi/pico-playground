add_executable(hello_sleep_alarm
        hello_sleep_alarm.c
        )
target_link_libraries(hello_sleep_alarm
        pico_stdlib
        hardware_sleep
        )
pico_add_extra_outputs(hello_sleep_alarm)

add_executable(hello_sleep_aon
        hello_sleep_aon.c
        )
target_link_libraries(hello_sleep_aon
        pico_stdlib
        hardware_sleep
        pico_aon_timer
        )
pico_add_extra_outputs(hello_sleep_aon)
