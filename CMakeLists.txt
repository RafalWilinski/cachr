cmake_minimum_required(VERSION 3.6)
project(caching_proxy)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES
    main.c)

add_executable(caching_proxy ${SOURCE_FILES})