if (TARGET pico_scanvideo_dpi)
    add_executable(scanvideo_minimal
            scanvideo_minimal.c
            )


    target_link_libraries(scanvideo_minimal PRIVATE
            pico_multicore
            pico_stdlib
            pico_scanvideo_dpi)
    pico_add_extra_outputs(scanvideo_minimal)
endif ()
