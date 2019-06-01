wayfire_sources = ['main.cpp',
                   'util.cpp',

                   'core/output-layout.cpp',
                   'core/opengl.cpp',
                   'core/plugin.cpp',
                   'core/core.cpp',
                   'core/img.cpp',
                   'core/wm.cpp',

                   'core/seat/input-inhibit.cpp',
                   'core/seat/input-manager.cpp',
                   'core/seat/keyboard.cpp',
                   'core/seat/cursor.cpp',
                   'core/seat/touch.cpp',
                   'core/seat/seat.cpp',

                   'view/surface.cpp',
                   'view/subsurface.cpp',
                   'view/view.cpp',
                   'view/view-impl.cpp',
                   'view/xdg-shell.cpp',
                   'view/xwayland.cpp',
                   'view/layer-shell.cpp',
                   'view/view-3d.cpp',
                   'view/compositor-view.cpp',

                   'output/plugin-loader.cpp',
                   'output/output.cpp',
                   'output/render-manager.cpp',
                   'output/workspace-impl.cpp',
                   'output/wayfire-shell.cpp',
                   'output/gtk-shell.cpp']

wayfire_dependencies = [wayland_server, wlroots, xkbcommon, libinput,
                       pixman, drm, egl, libevdev, glesv2, glm, wf_protos,
                       wfconfig, libinotify, backtrace]

if conf_data.get('BUILD_WITH_IMAGEIO')
    wayfire_dependencies += [jpeg, png]
endif

executable('wayfire', wayfire_sources,
        dependencies: wayfire_dependencies,
        include_directories: [wayfire_conf_inc, wayfire_api_inc],
        link_args: '-ldl',
        install: true)

install_headers(['api/nonstd/safe-list.hpp',
                 'api/nonstd/noncopyable.hpp',
                 'api/nonstd/observer_ptr.h',
                 'api/nonstd/reverse.hpp'],
                subdir: 'wayfire/nonstd')

install_headers(['api/compositor-surface.hpp',
                 'api/compositor-view.hpp',
                 'api/core.hpp',
                 'api/debug.hpp',
                 'api/decorator.hpp',
                 'api/img.hpp',
                 'api/object.hpp',
                 'api/opengl.hpp',
                 'api/output.hpp',
                 'api/plugin.hpp',
                 'api/render-manager.hpp',
                 'api/signal-definitions.hpp',
                 'api/util.hpp',
                 'api/view-transform.hpp',
                 'api/view.hpp',
                 'api/workspace-manager.hpp',
                 'api/input-device.hpp',
                 'api/output-layout.hpp'],
                subdir: 'wayfire')

pkgconfig = import('pkgconfig')
pkgconfig.generate(
    subdirs:      'wayfire',
    version:      meson.project_version(),
    filebase:     meson.project_name(),
    name:         meson.project_name(),
    description: 'A Wayland Compositor')
