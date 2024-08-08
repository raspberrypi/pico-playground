if (NOT PICO_NO_HARDWARE)
    add_subdirectory_exclude_platforms(hello_dormant "rp2350.*")
    add_subdirectory_exclude_platforms(hello_sleep "rp2350.*")
endif ()
