if (TARGET pico_scanvideo_dpi)
    add_executable(hscroll_dma_tiles
            hscroll_dma_tiles.c
            data.c
            data.h
            )

    add_compile_definitions(hscroll_dma_tiles PRIVATE
    #        DISABLE_HPIXELS
            PICO_SCANVIDEO_PLANE1_VARIABLE_FRAGMENT_DMA=1
            PICO_SCANVIDEO_USE_RAW1P_2CYCLE=1
            PICO_SCANVIDEO_PLANE_COUNT=2
            PICO_SCANVIDEO_PLANE2_VARIABLE_FRAGMENT_DMA=1
            PICO_SCANVIDEO_MAX_SCANLINE_BUFFER2_WORDS=200
            PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT=5
            PICO_SCANVIDEO_ENABLE_VIDEO_CLOCK_DOWN=1
            )

    target_link_libraries(hscroll_dma_tiles PRIVATE
            pico_stdlib
            pico_multicore
            pico_scanvideo_dpi
            render)
    pico_add_extra_outputs(hscroll_dma_tiles)
endif()