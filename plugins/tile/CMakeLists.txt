cmake_minimum_required(VERSION 3.1.0)
project(tile CXX)

file(GLOB SRC "tile.cpp")
add_library(tile SHARED ${SRC})

install(TARGETS   tile    DESTINATION lib/wayfire/)
