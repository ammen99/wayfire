# [Wayfire]

[Wayfire]: https://wayfire.org

![Version](https://img.shields.io/github/v/release/WayfireWM/wayfire)
[![IRC: #wayfire on freenode](https://img.shields.io/badge/irc-%23wayfire-informational)](https://webchat.freenode.net/#wayfire)
[![CI](https://github.com/WayfireWM/wayfire/workflows/CI/badge.svg)](https://github.com/WayfireWM/wayfire/actions)
[![Packaging status](https://repology.org/badge/tiny-repos/wayfire.svg)](https://repology.org/project/wayfire/versions)
[![License](https://img.shields.io/github/license/WayfireWM/wayfire)](LICENSE)

###### [Get started] | [Manual] | [Configuration]

[Get started]: https://github.com/WayfireWM/wayfire/wiki/Tutorial
[Manual]: https://github.com/WayfireWM/wayfire/wiki/General
[Configuration]: https://github.com/WayfireWM/wayfire/wiki/Configuration

Wayfire is a 3D [Wayland] compositor, inspired by [Compiz] and based on [wlroots].

It aims to create a customizable, extendable and lightweight environment without sacrificing its appearance.

[![Wayfire demos](https://img.youtube.com/vi_webp/2PtNzxDsxYM/maxresdefault.webp)](https://youtube.com/playlist?list=PLb7YRKEhWEBUIoT-a29UoJW9mhfzjpNle "YouTube – Wayfire demos")
[![YouTube Play Button](https://www.iconfinder.com/icons/317714/download/png/16)](https://youtube.com/playlist?list=PLb7YRKEhWEBUIoT-a29UoJW9mhfzjpNle) · [Wayfire demos](https://youtube.com/playlist?list=PLb7YRKEhWEBUIoT-a29UoJW9mhfzjpNle)

[Wayland]: https://wayland.freedesktop.org
[wlroots]: https://github.com/swaywm/wlroots
[Compiz]: https://launchpad.net/compiz

## Dependencies

- [Cairo](https://cairographics.org)
- [FreeType](https://freetype.org)
- [GLM](https://glm.g-truc.net)
- [libdrm](https://dri.freedesktop.org/wiki/DRM/)
- [libevdev](https://freedesktop.org/wiki/Software/libevdev/)
- [libGL](https://mesa3d.org)
- [libinput](https://freedesktop.org/wiki/Software/libinput/)
- [libjpeg](https://libjpeg-turbo.org)
- [libpng](http://libpng.org/pub/png/libpng.html)
- [libxkbcommon](https://xkbcommon.org)
- [Pixman](https://pixman.org)
- [pkg-config](https://freedesktop.org/wiki/Software/pkg-config/)
- [Wayland](https://wayland.freedesktop.org)
- [wayland-protocols](https://gitlab.freedesktop.org/wayland/wayland-protocols)
- [wf-config](https://github.com/WayfireWM/wf-config)
- [wf-shell](https://github.com/WayfireWM/wf-shell) (optional)
- [wlroots](https://github.com/swaywm/wlroots)

## Installation

The easiest way to install Wayfire, wf-shell and WCM to get a functional desktop is to use the [install scripts](https://github.com/WayfireWM/wf-install).

Alternatively, you can build from source:

``` sh
meson build
ninja -C build
sudo ninja -C build install
```

**Note**: `wf-config` and `wlroots` can be built as submodules, by specifying
`-Duse_system_wfconfig=disabled` and `-Duse_system_wlroots=disabled` options to `meson`.
This is the default if they are not present on your system.

###### Arch Linux

[wayfire] and [wayfire-git] are available in the [AUR].

``` sh
pacman -S wayfire
```

[AUR]: https://aur.archlinux.org
[wayfire]: https://aur.archlinux.org/packages/wayfire/
[wayfire-git]: https://aur.archlinux.org/packages/wayfire-git/

###### Exherbo

``` sh
cave resolve -x wayfire
```

###### Fedora

``` sh
dnf install wayfire
```

###### FreeBSD

``` sh
pkg install wayfire
```

###### Gentoo
Available from either of the two overlays [Guru](https://wiki.gentoo.org/wiki/Project:GURU) or [wayfire-desktop](https://github.com/epsilon-0/wayfire-desktop).  
Look at [wayfire-desktop](https://github.com/epsilon-0/wayfire-desktop) to get full list of options and available wayfire packages.  

###### NixOS

See [nixpkgs-wayland].

[nixpkgs-wayland]: https://github.com/colemickens/nixpkgs-wayland

###### Ubuntu

See the [build instructions][Ubuntu build instructions] from [@soreau].

[@soreau]: https://github.com/soreau
[Ubuntu build instructions]: http://blog.northfield.ws/wayfire/

###### Void

``` sh
xbps-install -S wayfire
```

## Configuration

Copy [`wayfire.ini`] to `~/.config/wayfire.ini`.
Before running Wayfire, you may want to change the command to start a terminal.
See the [Configuration] document for information on the options.

[`wayfire.ini`]: wayfire.ini

## Running

Run [`wayfire`][Manual] from a TTY, or via a Wayland-compatible login manager.
