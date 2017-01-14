cmake_minimum_required(VERSION 3.5)
project(cachr)

set (VERSION_MAJOR 0)
set (VERSION_MINOR 1)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

set(SOURCE_FILES
        src/ini.c
        src/ini.h
        src/error.c
        src/error.h
        main.c
        src/stack.c src/stack.h)

add_executable(cachr ${SOURCE_FILES})
