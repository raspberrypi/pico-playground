if (TARGET pico_scanvideo) # not all build types support it
    # Libs
    add_subdirectory_exclude_platforms(render)
    add_subdirectory_exclude_platforms(sprite)
    # Apps
    add_subdirectory_exclude_platforms(demo1)
    add_subdirectory_exclude_platforms(demo2 "rp2040")
    add_subdirectory_exclude_platforms(flash_stream "rp2350.*")
    add_subdirectory_exclude_platforms(hscroll_dma_tiles)
    add_subdirectory_exclude_platforms(mandelbrot)
    add_subdirectory_exclude_platforms(mario_tiles)
    add_subdirectory_exclude_platforms(scanvideo_minimal)
    add_subdirectory_exclude_platforms(sprite_demo "rp2350-riscv")
    add_subdirectory_exclude_platforms(test_pattern)
    add_subdirectory_exclude_platforms(textmode)
endif()