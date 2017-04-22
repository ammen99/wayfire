cmake_minimum_required(VERSION 3.1.0)

file(GLOB SOURCES "common.cpp" "background.cpp" "panel.cpp" "main.cpp")

find_package(PkgConfig REQUIRED)
pkg_check_modules(REQLIBS freetype2 wayland-client wayland-egl cairo egl wayland-cursor)

add_definitions(${REQLIBS_CFLAGS})
add_executable(wayfire-shell-client ${SOURCES})

# Libraries
target_link_libraries(wayfire-shell-client wayfire-shell-proto)
target_link_libraries(wayfire-shell-client ${REQLIBS_LIBRARIES})

install(TARGETS wayfire-shell-client DESTINATION lib/wayfire/)
