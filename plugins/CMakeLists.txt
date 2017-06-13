cmake_minimum_required(VERSION 3.1.0)

include_directories(INCLUDE_CONFIG_H)
# TODO: implement all plugins
add_subdirectory(single_plugins)
add_subdirectory(backlight)
#add_subdirectory(cube)
add_subdirectory(animate)
