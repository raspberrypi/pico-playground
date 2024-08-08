cmake_minimum_required(VERSION 3.12)

# Pull in PICO SDK (must be before project)
include(pico_sdk_import.cmake)

# We also need PICO EXTRAS
include(pico_extras_import.cmake)

project(pico_playground C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

if (PICO_SDK_VERSION_STRING VERSION_LESS "2.0.0")
    message(FATAL_ERROR "Require at least Raspberry Pi Pico SDK version 2.0.0")
endif()

function(add_subdirectory_exclude_platforms NAME)
    if (ARGN)
        if (PICO_PLATFORM IN_LIST ARGN)
            message("Skipping ${NAME} example which is unsupported on this platform")
            return()
        endif()
        foreach(PATTERN IN LISTS ARGN)
            string(REGEX MATCH "${PATTERN}" MATCHED "${PICO_PLATFORM}")
            if (MATCHED)
                message("Skipping ${NAME} example which is unsupported on this platform")
                return()
            endif()
        endforeach()
    endif()
    add_subdirectory(${NAME})
endfunction()

add_subdirectory(audio)
add_subdirectory(apps)
add_subdirectory(reset)
add_subdirectory(scanvideo)
add_subdirectory(sleep)
add_subdirectory(stdio)
