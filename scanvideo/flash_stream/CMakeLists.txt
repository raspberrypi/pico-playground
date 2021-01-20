if (PICO_ON_DEVICE AND TARGET pico_scanvideo_dpi)
    add_executable(flash_stream
            flash_stream.c
            )
    target_link_libraries(flash_stream PRIVATE
            pico_stdlib
            pico_scanvideo_dpi
            hardware_dma
            )
    target_compile_definitions(flash_stream PRIVATE
            PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=500
            )
    pico_add_extra_outputs(flash_stream)
endif ()
