if (TARGET pico_scanvideo_dpi)
    add_executable(test_pattern
            test_pattern.c
            )

    target_link_libraries(test_pattern PRIVATE
            pico_multicore
            pico_stdlib
            pico_scanvideo_dpi)

    pico_add_extra_outputs(test_pattern)

    pico_enable_stdio_uart(test_pattern 1)
    pico_enable_stdio_usb(test_pattern 1)
endif ()
