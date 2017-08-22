cmake_minimum_required(VERSION 3.1.0)
project(animate CXX)

file(GLOB SRC "animate.cpp" "fire.cpp" "particle.cpp")
add_library(animate SHARED ${SRC})

install(TARGETS   animate    DESTINATION lib/wayfire/)
install(DIRECTORY shaders    DESTINATION share/wayfire/animate)
