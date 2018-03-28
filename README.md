# Wayfire

**This is the wlroots branch, it doesn't work yet, try the master branch if you want to try Wayfire**

# Introduction

[![Join the chat at https://gitter.im/Wayfire-WM/Lobby](https://badges.gitter.im/Wayfire-WM/Lobby.svg)](https://gitter.im/Wayfire-WM/Lobby?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

Wayfire is a wayland compositor based on libweston. It aims to create a customizable, extendable and lightweight environment without sacrificing its appearance. If you want to gain a better impression at what it can do, see [screenshots](https://github.com/ammen99/wayfire/wiki/Screenshots), [list of plugins](https://github.com/ammen99/wayfire/wiki/Configuration-&-Plugins)

# Build and install

Unfortunately, the upstream libweston library doesn't have all the necessary functionality needed by wayfire(mainly it doesn't support custom rendering), so you need to compile a patched version, which you can find here: [libweston](https://github.com/ammen99/weston).
Here is a list of the changes included: [applied patches](https://github.com/ammen99/wayfire/wiki/Libweston-changes)

Note: if you install as shown below, it will likely overwrite the weston package provided by your distro. If you don't want to overwrite it, then you can install the patched weston in another location, for example `/opt/weston`. In this case don't forget to specify `PKG_CONFIG_PATH` when running cmake and then `LD_LIBRARY_PATH` when running `wayfire`.

```
git clone https://github.com/ammen99/weston && cd weston
./autogen.sh --prefix=/usr
make -j4 && sudo make install
```

To build wayfire, you'll need `cmake`, `glm`, `gio-2.0`, `freetype2`, `cairo` and the build dependencies for libweston(which you should have already installed in the previous step). Optionally(and it is recommended) to install `alsa-libs` and `alsa-utils` for managing sound(wayfire-sound-popup), `gdk-2.0`/`gtk2` and `gdk-pixbuf-2.0`(for loading jpeg backgrounds). When ready, simply clone this repo, compile and install:

```
git clone https://github.com/ammen99/wayfire && cd wayfire
mkdir build && cd build
cmake ../ -DCMAKE_BUILD_TYPE=Release -DUSE_GLES32=False
make -j4 && sudo make install
```
If your system has OpenGL ES 3.2 headers and supports tesselation shaders, then you can also compile with support for it(currently just adds deformation to the cube plugin):
```
git clone https://github.com/ammen99/wayfire && cd wayfire
mkdir build && cd build
cmake ../ -DCMAKE_BUILD_TYPE=Release -DUSE_GLES32=True
make -j4 && sudo make install
```

**Before running Wayfire, copy the default configuration file which is located in the root of the repository and place it in:**
```
cp wayfire.ini.default ~/.config/wayfire.ini
```

You can adjust background, panel properties (font family/size, which launchers to use, etc.) and key/button bindings in this file to your liking. To start wayfire, just execute `wayfire` from a TTY. If you encounter any issues, please read [debug report guidelines](https://github.com/ammen99/wayfire/wiki/Debugging-problems) and open a bug in this repo. You can also write in gitter.
# Project status

**IMPORTANT**: Although many of the features one can expect from a WM are implemented, Wayfire should be considered as **WIP** and **pre-alpha**. In my setup it works flawlessly, but this project hasn't been thoroughly tested, so there are a lot of bugs to be expected and to be fixed.

Currently supported:
1. Seamless integration of both native wayland & Xwayland clients
2. Workspaces (or more like viewports if you are familiar with compiz)
3. Configurable bindings
4. Floating and tiling mode(the latter being experimental)
5. Various plugins: Desktop cube, Expo(live workspace previews), Grid(arrange floating windows in a grid), Auto snap at edges, etc. See *plugins*
6. Shell panel with launchers, date, internet connection & battery support
7. Basic touchscreen gestures

Experimental/WIP:
1. On-screen keyboard
2. Tiling window mode
3. Window-rules plugin
4. Output rotation & scaling
5. Multiple outputs - currently supports static configurations, hotplugging is not tested very well 
6. Sane default config

Future plans:
1. Panel improvement
2. Better touchscreen abilities
3. Out-of-tree plugins
4. Packaging & better feature detection when building
5. Wayland and X11 nested backends - currently rendering is "a bit" off, so these are not really usable
6. Anything else


# Contributing to the project

I am currently the only one working on Wayfire, so contributions of any kind are welcome! There are many ways you can help, aside from developing - open bug reports, test features, add documentation, etc.

If you want to write your own plugin, a general outline of how the plugin system works is here: [plugin architecture](https://github.com/ammen99/wayfire/wiki/Plugin-architecture). Sadly, I don't have much time so there's close to none documentation. You can take a look at the simpler plugins(in `plugins/single_plugins`, the simplest are `command`, `screenshot`, `rotator`, around 50-60 loc each). Don't hesitate to write in gitter if you have any questions.
