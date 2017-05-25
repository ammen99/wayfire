cmake_minimum_required(VERSION 3.1.0)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

include_directories(SYSTEM /usr/include/libevdev-1.0)

file(GLOB SOURCES "*.cpp")

add_executable(wayfire ${SOURCES})
target_link_libraries(wayfire jpeg GLESv2)
target_link_libraries(wayfire dl wayland-server weston-3 weston-desktop-3 png xkbcommon input pixman-1 evdev EGL)
target_link_libraries(wayfire wayfire-shell-proto)
target_link_libraries(wayfire wayfire-config)

install(TARGETS wayfire DESTINATION bin)

