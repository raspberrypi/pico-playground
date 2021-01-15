cmake_minimum_required(VERSION 3.9)
project(converter)

set(CMAKE_CXX_STANDARD 17)

include_directories(../libs/render src)
add_executable(converter
        src/convert.cpp
        )
