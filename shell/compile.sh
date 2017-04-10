g++ -std=c++11 -g wayfire-shell-code.o panel.cpp common.cpp main.cpp `pkg-config --cflags freetype2` `pkg-config --libs wayland-egl wayland-client cairo egl freetype2` -o panel
