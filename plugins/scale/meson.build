all_include_dirs = [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc, vswitch_inc, wobbly_inc]
all_deps = [wlroots, pixman, wfconfig, wftouch]

shared_module('scale', 'scale.cpp',
        include_directories: all_include_dirs,
        dependencies: all_deps,
        install: true,
        install_dir: conf_data.get('PLUGIN_PATH'))
