cmake_minimum_required(VERSION 3.1.0)
find_package(PkgConfig REQUIRED)

pkg_check_modules(WFREQLIBS REQUIRED wayland-server libweston-3 libweston-desktop-3
                                     xkbcommon libinput pixman-1 egl libevdev glesv2)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

file(GLOB SOURCES "*.cpp")

if (NOT BUILD_WITH_IMAGEIO)
    list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/img.cpp)
endif(NOT BUILD_WITH_IMAGEIO)

link_directories(${WFREQLIBS_LIBRARY_DIRS})
include_directories(${WFREQLIBS_INCLUDE_DIRS})
add_definitions(${WFREQLIBS_CFLAGS_OTHER})

add_executable(wayfire ${SOURCES})

target_link_libraries(wayfire wayfire-shell-proto)
target_link_libraries(wayfire wayfire-config)
target_link_libraries(wayfire ${WFREQLIBS_LIBRARIES})

if (BUILD_WITH_IMAGEIO)
    target_link_libraries(wayfire ${IMAGEIO_LIBS_LIBRARIES})
endif (BUILD_WITH_IMAGEIO)

target_link_libraries(wayfire dl)

install(TARGETS wayfire DESTINATION bin)

