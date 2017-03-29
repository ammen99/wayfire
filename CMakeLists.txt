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
include_directories(SYSTEM /usr/include/libevdev-1.0)
file(GLOB SOURCES "src/*.cpp")

add_executable(wayfire ${SOURCES})

# Libraries
target_link_libraries(wayfire jpeg GLESv2)
target_link_libraries(wayfire dl wayland-server weston-3 weston-desktop-3 png xkbcommon input pixman-1 evdev EGL)

# Installation
install(TARGETS wayfire DESTINATION bin)
install(DIRECTORY shaders/ DESTINATION share/wayfire/shaders)

# Plugins
add_subdirectory(plugins)
