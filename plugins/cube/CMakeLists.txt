cmake_minimum_required(VERSION 3.1.0)
project(cube CXX)

add_library(cube  SHARED "cube.cpp")

install(TARGETS   cube             DESTINATION lib/wayfire/)
install(DIRECTORY shaders_2.0/     DESTINATION share/wayfire/cube/shaders_2.0)
#install(DIRECTORY shaders_3.3/ DESTINATION share/wayfire/cube/s3.3)
