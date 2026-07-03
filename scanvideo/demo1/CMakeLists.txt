if (TARGET pico_scanvideo_dpi)
    add_executable(demo1
            demo1.c
            data.c
            data.h
            )

    target_compile_definitions(demo1
            PRIVATE
            #PICO_SCANVIDEO_48MHZ=1
            # commented out as video overlay is distracting
            # PICO_SCANVIDEO_PLANE_COUNT=3
            )

    # if you want to use hardware spin locks on RP2350 there are a limited number of safe spin locks to use
    # so you must specify which to use (or pass PICO_SCANVIDEO_RP2350_OVERLAY_SDK_SPINLOCKS=1 as per here)
#    if (PICO_RP2350)
#        target_compile_definitions(demo1 PRIVATE
#                PICO_USE_SW_SPIN_LOCKS=0
#                PICO_SCANVIDEO_RP2350_OVERLAY_SDK_SPINLOCKS=1
#        )
#    endif()
    target_link_libraries(demo1 PRIVATE pico_stdlib pico_scanvideo_dpi render pico_multicore)
    pico_add_extra_outputs(demo1)
endif()