# only build I2S example if library is available
if (TARGET pico_audio_i2s)
    add_executable(sine_wave_i2s
            sine_wave.c
            )

    target_link_libraries(sine_wave_i2s PRIVATE
            pico_stdlib
            pico_audio_i2s
            )

    target_compile_definitions(sine_wave_i2s PRIVATE
            # compile time configuration of I2S
            PICO_AUDIO_I2S_MONO_INPUT=1
            #define for our example code
            USE_AUDIO_I2S=1
#            PICO_AUDIO_I2S_DATA_PIN=22
#            PICO_AUDIO_I2S_CLOCK_PIN_BASE=23
#            PICO_DEFAULT_UART=0
#            PICO_DEFAULT_UART_TX_PIN=28
#            PICO_DEFAULT_UART_RX_PIN=29
            )
    # create map/bin/hex file etc.
    pico_add_extra_outputs(sine_wave_i2s)
endif ()

# only build PWM example if library is available
if (TARGET pico_audio_pwm)
    add_executable(sine_wave_pwm
            sine_wave.c
            )

    target_link_libraries(sine_wave_pwm PRIVATE
            pico_stdlib
            pico_audio_pwm
            )

    target_compile_definitions(sine_wave_pwm PRIVATE
            #define for our example code
            USE_AUDIO_PWM=1
            )
    # create map/bin/hex file etc.
    pico_add_extra_outputs(sine_wave_pwm)
endif ()

# only build S/PDIF example if library is available
if (TARGET pico_audio_spdif)
    add_executable(sine_wave_spdif
            sine_wave.c
            )

    target_link_libraries(sine_wave_spdif PRIVATE
            pico_stdlib
            pico_audio_spdif
            )

    target_compile_definitions(sine_wave_spdif PRIVATE
            # compile time configuration of S/PDIF
            PICO_AUDIO_SPDIF_MONO_INPUT=1
            #define for our example code
            USE_AUDIO_SPDIF=1
            )
    # create map/bin/hex file etc.
    pico_add_extra_outputs(sine_wave_spdif)
endif ()
