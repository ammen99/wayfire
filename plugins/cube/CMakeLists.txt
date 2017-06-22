cmake_minimum_required(VERSION 3.1.0)
project(cube CXX)

add_library(cube SHARED "cube.cpp")

install(TARGETS cube DESTINATION lib/wayfire/)

if (USE_GLES32)
    install(DIRECTORY shaders_3.2/ DESTINATION share/wayfire/cube/shaders_3.2)
else (USE_GLES32)
    install(DIRECTORY shaders_2.0/ DESTINATION share/wayfire/cube/shaders_2.0)
endif(USE_GLES32)
