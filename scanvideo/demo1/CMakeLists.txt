if (TARGET pico_scanvideo_dpi)
    add_executable(demo1
            demo1.c
            data.c
            data.h
            )

    target_compile_definitions(demo1
            PRIVATE
            # video overlay is distracting
#            PICO_SCANVIDEO_PLANE_COUNT=3
            )

    target_link_libraries(demo1 PRIVATE pico_stdlib pico_scanvideo_dpi render pico_multicore)
    pico_add_extra_outputs(demo1)
endif()