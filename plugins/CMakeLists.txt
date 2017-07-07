cmake_minimum_required(VERSION 3.1.0)

include_directories(INCLUDE_CONFIG_H)
pkg_check_modules(PLUGINS REQUIRED pixman-1 libweston-3)

link_directories(${PLUGINS_LIBRARY_DIRS})
include_directories(${PLUGINS_INCLUDE_DIRS})
add_definitions(${PLUGINS_CFLAGS_OTHER})

add_subdirectory(single_plugins)
add_subdirectory(backlight)
add_subdirectory(cube)
add_subdirectory(animate)
