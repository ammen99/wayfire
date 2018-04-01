libwfwindow = static_library('wfwindow',
    ['window.cpp', 'shm-surface.cpp'],
    dependencies: [cairo, wf_protos],
    include_directories: wayfire_conf_inc)
wfwindow = declare_dependency(link_with: libwfwindow)

shell_deps = [wf_protos, wfconfig, wfwindow,
      freetype2, cairo, wayland_client, wayland_cursor]

panel = executable('wayfire-shell-panel',
    ['panel.cpp', 'widgets.cpp', 'net.cpp', 'gamma.cpp', 'main.cpp'],
    dependencies: shell_deps + [gio, threads],
    include_directories: wayfire_conf_inc,
    install: true,
    install_dir: 'lib/wayfire')

background_deps = shell_deps

if conf_data.get('BUILD_WITH_PIXBUF', false)
  background_deps += [gdk, pixbuf]
endif

background = executable('wayfire-shell-background',
    ['background.cpp'],
    dependencies: background_deps,
    include_directories: wayfire_conf_inc,
    install: true,
    install_dir: 'lib/wayfire')

#vkeyboard = executable('wayfire-virtual-keyboard',
#    ['vkeyboard.cpp'],
#    dependencies: shell_deps + [libevdev],
#    include_directories: wayfire_conf_inc,
#    install: true,
#    install_dir: 'lib/wayfire')
#
if conf_data.get('BUILD_WITH_ALSA', false)
  sound_popup = executable('wayfire-sound-popup',
      ['sound-popup.cpp'],
      dependencies: shell_deps + [alsa],
      include_directories: wayfire_conf_inc,
      install: true,
      install_dir: 'lib/wayfire')
endif




