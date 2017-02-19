cmake_minimum_required(VERSION 3.5)
project(cachr)

set (VERSION_MAJOR 0)
set (VERSION_MINOR 1)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

set(SOURCE_FILES
        src/libs/ini.c
        src/libs/ini.h
        src/error.c
        src/error.h
        src/main.c
        src/libs/picohttpparser.c src/libs/picohttpparser.h src/netutils.c src/configutils.c src/configutils.h src/netutils.h src/utils.c src/utils.h)

add_executable(cachr ${SOURCE_FILES})
