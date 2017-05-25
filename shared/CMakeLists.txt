cmake_minimum_required(VERSION 3.1.0)

include_directories(SYSTEM /usr/include/libevdev-1.0)
add_library(wayfire-config STATIC "config.cpp")
