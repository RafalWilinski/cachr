cmake_minimum_required(VERSION 3.5)
project(cachr)

set (VERSION_MAJOR 0)
set (VERSION_MINOR 1)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

set(SOURCE_FILES
        src/dict.c
        src/dict.h
        src/ini.c
        src/ini.h
        src/error.c
        src/error.h
        main.c
        )

add_executable(cachr ${SOURCE_FILES})
