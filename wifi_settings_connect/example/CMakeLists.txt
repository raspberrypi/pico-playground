#
# Copyright (c) 2025 Jack Whitham
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Example for wifi_settings_connect.
#
# This example connects to WiFi using wifi_settings_connect functions,
# and then broadcasts a message on UDP port 1234 every second.
# You can receive these with any tool that can receive
# UDP, e.g. tcpdump, Wireshark, or netcat:
#
#   nc -l -u -p 1234
#
# The WiFi connection details must be configured in Flash as described here:
# https://github.com/jwhitham/pico-wifi-settings/blob/master/doc/SETTINGS_FILE.md
#

# only build wifi_settings_connect example if library is available
if (TARGET wifi_settings_connect)
    # Build example with async_context tasks running in the background
    # You can use pico_cyw43_arch_lwip_poll instead of
    # pico_cyw43_arch_lwip_threadsafe_background to use polling, if you prefer.
    add_executable(wifi_settings_connect_example
            example.c
        )
    target_include_directories(wifi_settings_connect_example PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}
        )
    target_link_libraries(wifi_settings_connect_example
            pico_cyw43_arch_lwip_threadsafe_background
            wifi_settings_connect 
            pico_stdlib
        )

    pico_enable_stdio_usb(wifi_settings_connect_example 1)
    pico_enable_stdio_uart(wifi_settings_connect_example 0)

    pico_add_extra_outputs(wifi_settings_connect_example)
endif ()
