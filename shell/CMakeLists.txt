cmake_minimum_required(VERSION 3.1.0)
find_package(PkgConfig REQUIRED)

file(GLOB SOURCES "window.cpp" "background.cpp" "panel.cpp" "main.cpp" "widgets.cpp" "gamma.cpp" "net.cpp")

if (HAS_CAIRO_GL_H)
    list(APPEND SOURCES "egl-surface.cpp")
    pkg_check_modules(EGL_SURF_BACKEND wayland-egl egl cairo-gl)
else (HAS_CAIRO_GL_H)
    list(APPEND SOURCES "shm-surface.cpp")
endif (HAS_CAIRO_GL_H)

pkg_check_modules(REQLIBS libevdev freetype2 wayland-client cairo wayland-cursor gio-2.0)

add_definitions(${REQLIBS_CFLAGS_OTHER})
link_directories(${REQLIBS_LIBRARY_DIRS})
include_directories(${REQLIBS_INCLUDE_DIRS})

if (HAS_CAIRO_GL_H)
    add_definitions(${EGL_SURF_BACKEND_CFLAGS_OTHER})
    link_directories(${EGL_SURF_BACKEND_LIBRARY_DIRS})
    include_directories(${EGL_SURF_BACKEND_INCLUDE_DIRS})
endif (HAS_CAIRO_GL_H)

add_executable(wayfire-shell-client ${SOURCES})

find_package (Threads)
target_link_libraries (wayfire-shell-client ${CMAKE_THREAD_LIBS_INIT})

# Libraries
target_link_libraries(wayfire-shell-client wayfire-shell-proto wayfire-config)
target_link_libraries(wayfire-shell-client ${REQLIBS_LIBRARIES})
if (HAS_CAIRO_GL_H)
    target_link_libraries(wayfire-shell-client ${EGL_SURF_BACKEND_LIBRARIES})
endif (HAS_CAIRO_GL_H)

install(TARGETS wayfire-shell-client DESTINATION lib/wayfire/)
