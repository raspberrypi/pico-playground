if (TARGET pico_scanvideo_dpi)
    add_executable(demo2 demo2.c data2.c)

    if (PICO_RP2350)
        target_compile_definitions(demo2 PRIVATE
                #PICO_SCANVIDEO_48MHZ=1
                PICO_USE_SW_SPIN_LOCKS=0
                PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT=16 # a few extra for amortization
                )
    endif()
    target_link_libraries(demo2 PRIVATE pico_stdlib pico_scanvideo_dpi pico_multicore render)
    pico_add_extra_outputs(demo2)
endif()