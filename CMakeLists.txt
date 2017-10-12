cmake_minimum_required(VERSION 3.1.0)

set(CMAKE_CXX_STANDARD 11)
add_compile_options(-Wall -Wextra -Wno-unused-parameter)
project(wayfire)

if(CMAKE_BUILD_TYPE MATCHES Debug)
    set(WAYFIRE_DEBUG_ENABLED TRUE)
else(CMAKE_BUILD_TYPE MATCHES Debug)
    set(WAYFIRE_DEBUG_ENABLED FALSE)
endif (CMAKE_BUILD_TYPE MATCHES Debug)

find_package(PkgConfig)

pkg_check_modules(IMAGEIO_LIBS libpng libjpeg)

set(BUILD_WITH_IMAGEIO ${PKG_CONFIG_FOUND})

if (NOT DEFINED USE_GLES32)
    set(USE_GLES32 TRUE)
endif(NOT DEFINED USE_GLES32)

if (NOT DEFINED HAS_CAIRO_GL_H)
    include(CheckIncludeFiles)
    CHECK_INCLUDE_FILES("cairo/cairo-gl.h" HAS_CAIRO_GL_H)
endif(NOT DEFINED HAS_CAIRO_GL_H)

if (HAS_CAIRO_GL_H)
    set(HAS_CAIRO_GL_H TRUE)
else (HAS_CAIRO_GL_H)
    set(HAS_CAIRO_GL_H FALSE)
endif (HAS_CAIRO_GL_H)

configure_file(config.h.in config.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

message("\n\nConfiguration options:\n")
message("    ImageIO: " ${BUILD_WITH_IMAGEIO})
message("    OpenGL ES 3.2: " ${USE_GLES32})
message("    Cario-GL: " ${HAS_CAIRO_GL_H})
message("    Debugging output: " ${WAYFIRE_DEBUG_ENABLED})
message("\n")

include_directories(SYSTEM /usr/include/pixman-1)
include_directories(src)
include_directories(shared)

add_subdirectory(proto)
add_subdirectory(shared)
add_subdirectory(src)

# Installation
install(DIRECTORY shaders/ DESTINATION share/wayfire/shaders)
install(FILES wayfire.desktop DESTINATION share/wayland-sessions)

# Plugins
add_subdirectory(plugins)

# Shell
add_subdirectory(shell)
