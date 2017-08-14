cmake_minimum_required(VERSION 3.1.0)

file(GLOB SOURCES "common.cpp" "background.cpp" "panel.cpp" "main.cpp" "widgets.cpp" "gamma.cpp" "net.cpp")

find_package(PkgConfig REQUIRED)
pkg_check_modules(REQLIBS libevdev freetype2 wayland-client wayland-egl cairo cairo-gl egl wayland-cursor gio-2.0)

add_definitions(${REQLIBS_CFLAGS_OTHER})
link_directories(${REQLIBS_LIBRARY_DIRS})
include_directories(${REQLIBS_INCLUDE_DIRS})
add_executable(wayfire-shell-client ${SOURCES})

find_package (Threads)
target_link_libraries (wayfire-shell-client ${CMAKE_THREAD_LIBS_INIT})

# Libraries
target_link_libraries(wayfire-shell-client wayfire-shell-proto wayfire-config)
target_link_libraries(wayfire-shell-client ${REQLIBS_LIBRARIES})

install(TARGETS wayfire-shell-client DESTINATION lib/wayfire/)
