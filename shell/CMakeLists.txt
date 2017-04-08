cmake_minimum_required(VERSION 3.1.0)

# TODO: when ready build type should not be Debug, but Release
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
file(GLOB SOURCES "main.cpp" "surface.cpp")

add_executable(wayfire_shell ${SOURCES})

# Libraries
target_link_libraries(wayfire_shell  )
target_link_libraries(wayfire_shell  )

# Installation
install(TARGETS wayfire DESTINATION bin)
install(DIRECTORY shaders/ DESTINATION share/wayfire/shaders)

# Plugins
add_subdirectory(plugins)
