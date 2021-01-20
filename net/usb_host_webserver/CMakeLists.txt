if (TARGET lwip)
    add_executable(usb_host_webserver
            main.c
    )

    target_include_directories(usb_host_webserver PRIVATE ${CMAKE_CURRENT_LIST_DIR})

    target_link_libraries(usb_host_webserver PRIVATE pico_stdlib tinyusb_host tinyusb_board lwip)
    pico_add_extra_outputs(usb_host_webserver)
endif()