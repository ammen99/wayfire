cmake_minimum_required(VERSION 3.1.0)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 11)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)
project(wayfire CXX)

# TODO: check for all packages, otherwise target_link_libraries might not work
# Find required package
find_package(PkgConfig)
pkg_check_modules(gl REQUIRED gl)

# Main executable
include_directories(src)
include_directories(SYSTEM /usr/include/pixman-1)
file(GLOB SOURCES "src/*.cpp")

add_executable(wayfire ${SOURCES})

# Libraries
target_link_libraries(wayfire jpeg GLESv2)
target_link_libraries(wayfire dl wayland-server weston-1 weston-desktop-1 png xkbcommon)
target_link_libraries(wayfire ${CMAKE_SOURCE_DIR}/src/libshared.a)

# Installation
install(TARGETS wayfire DESTINATION bin)
install(DIRECTORY shaders/ DESTINATION share/wayfire/shaders)

# Plugins
# add_subdirectory(plugins)
