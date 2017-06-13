cmake_minimum_required(VERSION 3.1.0)
find_package(PkgConfig REQUIRED)

pkg_check_modules(WFREQLIBS wayland-server libweston-3 libweston-desktop-3 xkbcommon libinput pixman-1 libevdev egl glesv2)
if(BUILD_WITH_IMAGEIO)
    pkg_check_modules(WFREQLIBS libpng libjpeg)
endif(BUILD_WITH_IMAGEIO)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

include_directories(SYSTEM /usr/include/libevdev-1.0)

file(GLOB SOURCES "*.cpp")

if (NOT BUILD_WITH_IMAGEIO)
    list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/img.cpp)
endif(NOT BUILD_WITH_IMAGEIO)

add_executable(wayfire ${SOURCES})

target_link_libraries(wayfire ${WFREQLIBS_LIBRARIES})
target_link_libraries(wayfire dl)
target_link_libraries(wayfire wayfire-shell-proto)
target_link_libraries(wayfire wayfire-config)

install(TARGETS wayfire DESTINATION bin)

