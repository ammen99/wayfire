cmake_minimum_required(VERSION 3.1.0)

set(CMAKE_CXX_STANDARD 11)

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
target_link_libraries(wayfire dl wlc png)

# Installation
install(TARGETS wayfire DESTINATION bin)
install(DIRECTORY shaders/ DESTINATION share/wayfire/shaders)

# Plugins
add_subdirectory(plugins)

