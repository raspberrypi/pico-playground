if (PICO_ON_DEVICE)
    if (TARGET pico_scanvideo_dpi AND TARGET pico_sd_card)
        add_executable(popcorn
                popcorn.c
                atlantis.c
                lcd12.c
                lcd18.c
                )

        add_compile_definitions(popcorn
                PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=164
                # seems fine without 16 (maybe need for overlay)
                PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT=16
                #PICO_DEBUG_MALLOC
                PICO_AUDIO_I2S_DMA_IRQ=1
                PICO_AUDIO_I2S_PIO=0
                PICO_STACK_SIZE=0x400
                __HEAP_SIZE=0x500
                PICO_USE_STACK_GUARDS=1
                PICO_SCANVIDEO_ADJUST_BUS_PRIORITY=1
                PICO_SCANVIDEO_ENABLE_VIDEO_CLOCK_DOWN=1
        #        VIDEO_TIME_CRITICAL_CODE_SECTION=.core0.video
                PLATYPUS_565
                VIDEO_565
                VIDEO_DBI

                PICO_SCANVIDEO_LINKED_SCANLINE_BUFFERS=1 # we do two rows at a time
                )

        if (PICO_BOARD STREQUAL "vgaboard")
            add_compile_definitions(popcorn PRIVATE
                PICO_DEFAULT_UART=0
                PICO_DEFAULT_UART_TX_PIN=28
                PICO_DEFAULT_UART_RX_PIN=29
            )
        endif()
        target_link_libraries(popcorn
                pico_multicore
                pico_stdlib
                platypus
                pico_scanvideo_dpi
                pico_sd_card
                pico_audio_i2s)
        pico_add_extra_outputs(popcorn)
    endif()
endif()