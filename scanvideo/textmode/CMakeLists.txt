PROJECT(textmode)

if (TARGET pico_scanvideo_dpi)
    add_executable(textmode
            textmode.c
            font6.c
            font8.c
            font10.c
            lcd.c
    )

    target_compile_definitions(textmode PRIVATE
            PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT=4
            PICO_SCANVIDEO_PLANE1_FIXED_FRAGMENT_DMA=true
            PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=250 # room for more characters
    )

    target_link_libraries(textmode PRIVATE
            pico_multicore
            pico_stdlib
            pico_scanvideo_dpi
            render)
    pico_add_extra_outputs(textmode)

    if (PICO_RP2350)
        target_compile_definitions(textmode PRIVATE
                PICO_USE_SW_SPIN_LOCKS=0
        )
    endif()
endif()