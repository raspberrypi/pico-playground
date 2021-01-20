add_library(render INTERFACE)

cmake_policy(SET CMP0076 NEW)
target_sources(render INTERFACE
        image.c
        image.h
        spans.c
        spans.h
        )

target_include_directories(render INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(render INTERFACE pico_base_headers)
