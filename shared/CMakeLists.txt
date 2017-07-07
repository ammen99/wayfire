cmake_minimum_required(VERSION 3.1.0)

find_package(PkgConfig REQUIRED)

pkg_check_modules(CONFLIBS REQUIRED libevdev libweston-3)

link_directories(${CONFLIBS_LIBRARY_DIRS})
include_directories(${CONFLIBS_INCLUDE_DIRS})
add_definitions(${CONFLIBS_CFLAGS_OTHER})

add_library(wayfire-config STATIC "config.cpp")
