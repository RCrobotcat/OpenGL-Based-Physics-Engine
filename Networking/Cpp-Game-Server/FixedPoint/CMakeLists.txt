cmake_minimum_required(VERSION 4.1)
project(deterministic)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

set(DETERMINISTIC_SOURCES
        deterministic_float.cpp
        deterministic_float.h
        glacier_float.cpp
        glacier_float.h
        glacier_platform.h)

if(APPLE)
    list(APPEND DETERMINISTIC_SOURCES main.mm)
else()
    list(APPEND DETERMINISTIC_SOURCES main.cpp)
endif()

add_executable(deterministic ${DETERMINISTIC_SOURCES})
