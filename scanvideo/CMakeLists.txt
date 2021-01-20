if (TARGET pico_scanvideo) # not all build types support it
    # Libs
    add_subdirectory(render)
    add_subdirectory(sprite)
    # Apps
    add_subdirectory(demo1)
    add_subdirectory(hscroll_dma_tiles)
    add_subdirectory(flash_stream)
    add_subdirectory(mandelbrot)
    add_subdirectory(mario_tiles)
    add_subdirectory(scanvideo_minimal)
    add_subdirectory(sprite_demo)
    add_subdirectory(textmode)
endif()