cmake_minimum_required(VERSION 3.1.0)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_COMPILER /usr/bin/clang)
set(CMAKE_CXX_COMPILER /usr/bin/clang++)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra -Wno-unused-parameter -Wno-c99-extensions -Wno-vla-extension -I/usr/include/pixman-1")

project(wayfire CXX)
# Find required package
find_package(PkgConfig)
pkg_check_modules(gl REQUIRED gl)

# Main executable
include_directories(src)
file(GLOB SOURCES "src/*.cpp")

add_executable(wayfire ${SOURCES})

# Libraries
target_link_libraries(wayfire jpeg GLESv2)
target_link_libraries(wayfire dl wlc wayland-server weston-1 weston-desktop-1 png xkbcommon)

# Installation
install(TARGETS wayfire DESTINATION bin)
install(DIRECTORY shaders/ DESTINATION share/wayfire/shaders)

# Plugins
# add_subdirectory(plugins)
