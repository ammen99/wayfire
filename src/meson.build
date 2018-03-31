wayfire_sources = ['main.cpp',   'core.cpp',   'view.cpp', 'output.cpp',
                   'opengl.cpp', 'plugin.cpp', 'wm.cpp']

wayfire_dependencies = [wayland_server, wlroots, xkbcommon, libinput,
                       pixman, egl, libevdev, glesv2, glm, wf_protos,
                       wfconfig]

if conf_data.get('BUILD_WITH_IMAGEIO')
    wayfire_sources += ['img.cpp']
    wayfire_dependencies += [jpeg, png]
endif

executable('wayfire', wayfire_sources,
        dependencies: wayfire_dependencies,
        include_directories: [wayfire_conf_inc, wayfire_api_inc],
        link_args: '-ldl',
        install: true)
