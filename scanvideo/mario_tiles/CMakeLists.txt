if (PICO_ON_DEVICE AND TARGET pico_scanvideo_dpi)
    add_executable(mario_tiles
            mario_tiles.c
            data.c
            data.h
            )

    add_compile_definitions(mario_tiles PRIVATE
            #        DISABLE_HPIXELS
            PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=200
            PICO_SCANVIDEO_PLANE_COUNT=2
            PICO_SCANVIDEO_PLANE2_VARIABLE_FRAGMENT_DMA=1
            PICO_SCANVIDEO_MAX_SCANLINE_BUFFER2_WORDS=200
            PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT=5
            PICO_SCANVIDEO_ENABLE_VIDEO_CLOCK_DOWN=1
            )

    target_link_libraries(mario_tiles PRIVATE
            pico_stdlib
            pico_multicore
            pico_scanvideo_dpi
            render)

    if (PICO_ON_DEVICE)
        target_link_libraries(mario_tiles PRIVATE
                hardware_interp)
    endif()
    pico_add_extra_outputs(mario_tiles)
endif()
