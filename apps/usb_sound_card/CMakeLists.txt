if (TARGET usb_device)
    PROJECT(usb_sound_card)

    add_executable(usb_sound_card
            usb_sound_card.c
            )

    target_compile_definitions(usb_sound_card PRIVATE
            AUDIO_FREQ_MAX=48000

            # ours are zero based, so say so
            PICO_USBDEV_USE_ZERO_BASED_INTERFACES=1

            # need large descriptor
            PICO_USBDEV_MAX_DESCRIPTOR_SIZE=256


            PICO_USBDEV_ISOCHRONOUS_BUFFER_STRIDE_TYPE=1
            PICO_USBDEV_ENABLE_DEBUG_TRAgCE

            PICO_AUDIO_I2S_MONO_OUTPUT=0
            PICO_AUDIO_I2S_MONO_INPUT=0
    )

    target_link_libraries(usb_sound_card pico_stdlib usb_device pico_audio_i2s pico_multicore)
    pico_add_extra_outputs(usb_sound_card)
endif()
