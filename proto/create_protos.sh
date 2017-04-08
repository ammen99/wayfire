#! /bin/sh

wayland-scanner client-header wayfire-shell.xml wayfire-shell-client.h
wayland-scanner server-header wayfire-shell.xml wayfire-shell-server.h
wayland-scanner code          wayfire-shell.xml wayfire-shell-code.c
