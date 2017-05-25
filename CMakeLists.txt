cmake_minimum_required(VERSION 3.1.0)

set(CMAKE_CXX_STANDARD 11)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)
project(wayfire)

# TODO: check for all packages, otherwise target_link_libraries might not work
# Find required package
find_package(PkgConfig)

include_directories(SYSTEM /usr/include/pixman-1)
include_directories(src)
include_directories(shared)

add_subdirectory(proto)
add_subdirectory(shared)
add_subdirectory(src)

# Installation
install(DIRECTORY shaders/ DESTINATION share/wayfire/shaders)

# Plugins
add_subdirectory(plugins)

# Shell
add_subdirectory(shell)
