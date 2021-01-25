# THIS IS A SLIGHTLY HACKY EXAMPLE PROJECT DESIGNED TO HELP YOU TOWARDS INCLUDING THE PICO SDK
# CODE OUTSIDE OF THE CMAKE BUILD
#
# Note that the Pico SDK is designed to make it easy for users to build applications rather to build
# other runtimes for the Pico.
#
# There is no included "static library" of the Pico SDK because all of the Pico SDK Libraries are configurable
# on a per binary basis, and are always compiled from scratch per binary within the normal Pico SDK CMake build
#
# Note you would want to customize this to add or remove the dependencies you want (e.g. you mau not want
# to depend on pico_stdlib, but rather pick and mix some of its constituents to give you a finer grain of control
#
# Note that this example produces a build_info.txt in the output with some approximate (we're not trying to
# rewrite CMake here!) compile and link flags based on the setup of your static_sdk library
#
# Finally note that even copying the build and link flags from build_info.txt and linking against the generated
# library does not get you all the way, as you still need a second stage boot at the start of the binary (0x000-0x100)

cmake_minimum_required(VERSION 3.12)

# Pull in PICO SDK (must be before project)
include(pico_sdk_import.cmake)

project(static_sdk C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

if (PICO_ON_DEVICE)
    include(hacky_cmake_helper.cmake)

    add_library(static_sdk
            dummy.c)

    target_compile_definitions(static_sdk PUBLIC
            PICO_NO_BUILD_TYPE_FEATURE=1)

    target_link_libraries(static_sdk PUBLIC pico_stdlib)

    # our hacky_cmake_helper does not deal with generator expressions for libraries, so pick some (assuming we want the pico versions)
    # you might choose not to have these libraries (which are mostly redirecting AEABI functions to our faster (possible ROM) equivalents
    # especially if you are skipping SDK initialization (which is required for some of them)
    target_link_libraries(static_sdk PUBLIC
            pico_printf_pico
            pico_bit_ops_pico
            pico_divider_hardware
            pico_float_pico
            pico_double_pico
            pico_mem_ops_pico
            pico_int64_ops_pico

            pico_stdio_uart
            #pico_stdio_usb
    )

    # Compile definitions
    gather_vars(COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS _touched_cd static_sdk)
    list(REMOVE_DUPLICATES COMPILE_DEFINITIONS)
    List(PREPEND COMPILE_DEFINITIONS "") # need a -D at the beginning
    string(REPLACE ";" " -D" COMPILE_DEFINITIONS "${COMPILE_DEFINITIONS}")

    # Compile options
    gather_vars(COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS _touched_cf static_sdk)
    list(REMOVE_DUPLICATES COMPILE_OPTIONS)
    string(REPLACE ";" " " COMPILE_OPTIONS "${COMPILE_OPTIONS}")
    # Note that the Pico SDK has some options specific to the language being compiled. All
    # SDK has already been compiled, so we'll just strip these and you'll have to deal with these yourselves
    string(REGEX REPLACE "\\$<\\$<COMPILE_LANGUAGE:CXX>:.*>" "" COMPILE_OPTIONS ${COMPILE_OPTIONS})

    # Include dirs
    gather_vars(INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES _touched_id static_sdk)
    list(REMOVE_DUPLICATES INCLUDE_DIRECTORIES)
    string(REPLACE ";" " " INCLUDE_DIRECTORIES "${INCLUDE_DIRECTORIES}")

    # Link options
    gather_vars(LINK_FLAGS INTERFACE_LINK_OPTIONS _touched_l static_sdk)
    string(REPLACE ";" " " LINK_FLAGS "${LINK_FLAGS}")
    string(REPLACE "LINKER:" "-Wl," LINK_FLAGS "${LINK_FLAGS}")

    # Target properties were scoped to the target being built, lets scope them to static_sdk
    string(REPLACE "$<TARGET_PROPERTY:" "$<TARGET_PROPERTY:static_sdk," COMPILE_DEFINITIONS "${COMPILE_DEFINITIONS}")
    string(REPLACE "$<TARGET_PROPERTY:" "$<TARGET_PROPERTY:static_sdk," COMPILE_OPTIONS "${COMPILE_OPTIONS}")
    string(REPLACE "$<TARGET_PROPERTY:" "$<TARGET_PROPERTY:static_sdk," INCLUDE_DIRECTORIES "${INCLUDE_DIRECTORIES}")
    string(REPLACE "$<TARGET_PROPERTY:" "$<TARGET_PROPERTY:static_sdk," LINK_FLAGS "${LINK_FLAGS}")

    # The only way to get CMake to do some generator expression evaluation is to generate a file
    file(GENERATE OUTPUT build_info.txt CONTENT
"Compile definitions: ${COMPILE_DEFINITIONS}
Compile options    : ${COMPILE_OPTIONS}
Include dirs       : ${INCLUDE_DIRECTORIES}
Link flags         : ${LINK_FLAGS}")

endif()