cmake_minimum_required(VERSION 3.26)
project(threadPool)

set(CMAKE_CXX_STANDARD 17)

set(HEADERS
        threadpool.h
)

set(SOURCES
        threadpool.cpp
        main.cpp
)

include_directories(.)

add_executable(threadPool ${HEADERS} ${SOURCES})
