cmake_minimum_required(VERSION 3.1.0)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/CMake)
set(CMAKE_CXX_STANDARD 11)

project(wayfire C CXX)
# Find required package
find_package(PkgConfig)
pkg_check_modules(gl REQUIRED gl)

# Main executable
include_directories(src)
file(GLOB SOURCES "src/*.cpp")

add_executable(wayfire ${SOURCES})

# Compiler options
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-fpermissive")

# Libraries
target_link_libraries(wayfire jpeg GLESv2)
target_link_libraries(wayfire dl wlc)

# Installation
install(TARGETS wayfire DESTINATION bin)
install(DIRECTORY shaders/ DESTINATION share/wayfire/shaders)

# Plugins
add_subdirectory(plugins)

