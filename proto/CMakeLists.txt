cmake_minimum_required(VERSION 3.1.0)

find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wayfire-shell-client.h
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header ${CMAKE_CURRENT_SOURCE_DIR}/wayfire-shell.xml
    ${CMAKE_CURRENT_BINARY_DIR}/wayfire-shell-client.h &&
    cp ${CMAKE_CURRENT_BINARY_DIR}/wayfire-shell-client.h ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS wayfire-shell.xml)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wayfire-shell-server.h
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} server-header ${CMAKE_CURRENT_SOURCE_DIR}/wayfire-shell.xml
    ${CMAKE_CURRENT_BINARY_DIR}/wayfire-shell-server.h &&
    cp ${CMAKE_CURRENT_BINARY_DIR}/wayfire-shell-server.h ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS wayfire-shell.xml)

add_custom_command(OUTPUT wayfire-shell-code.c
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} code ${CMAKE_CURRENT_SOURCE_DIR}/wayfire-shell.xml ${CMAKE_CURRENT_BINARY_DIR}/wayfire-shell-code.c
    DEPENDS wayfire-shell-server.h wayfire-shell-client.h)

add_library(wayfire-shell-proto STATIC "wayfire-shell-code.c")
