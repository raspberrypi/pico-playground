if (PICO_ON_DEVICE AND TARGET pico_scanvideo_dpi)
    add_executable(sprite_demo
            sprite_demo.c
            )

    target_compile_definitions(sprite_demo PRIVATE
            PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=500
            )
    target_link_libraries(sprite_demo PRIVATE
            pico_multicore
            pico_stdlib
            pico_scanvideo_dpi
            sprite)
    pico_add_extra_outputs(sprite_demo)
endif ()
