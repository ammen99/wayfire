cmake_minimum_required(VERSION 3.1.0)
project(cube CXX)

add_library(cube  SHARED "cube.cpp")

install(TARGETS   cube         DESTINATION lib/fireman/)
install(DIRECTORY shaders/     DESTINATION share/fireman/cube/s4.0)
install(DIRECTORY shaders_3.3/ DESTINATION share/fireman/cube/s3.3)
