if (TARGET pico_scanvideo_dpi)
    add_executable(mandelbrot
            mandelbrot.c
            )

    target_compile_definitions(mandelbrot PRIVATE
            PICO_SCANVIDEO_PLANE1_VARIABLE_FRAGMENT_DMA=1
            #TURBO_BOOST=1
            )
    target_link_libraries(mandelbrot PRIVATE
            pico_stdlib
            pico_scanvideo_dpi
            pico_multicore)
    pico_add_extra_outputs(mandelbrot)
endif ()
