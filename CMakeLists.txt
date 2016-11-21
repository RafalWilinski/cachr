cmake_minimum_required(VERSION 3.5  )
project(caching_proxy)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O3")

set(SOURCE_FILES
        src/dict.c
        src/dict.h
        src/main.c
        )

add_executable(caching_proxy ${SOURCE_FILES})