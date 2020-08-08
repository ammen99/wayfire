#include <cstdlib>
#include <cstring>
#include <iostream>
#include <getopt.h>
#include <signal.h>
#include <map>

#include <sys/inotify.h>
#include <unistd.h>

#include "debug-func.hpp"
#include "main.hpp"
#include "wayfire/nonstd/safe-list.hpp"
#include <wayfire/config/file.hpp>

extern "C"
{
#define static
#include <wlr/render/gles2.h>
#undef static
#include <wlr/backend/multi.h>
#include <wlr/backend/wayland.h>
#include <wlr/util/log.h>
}

#include <wayland-server.h>

#include "core/core-impl.hpp"
#include "view/view-impl.hpp"
#include "wayfire/output.hpp"

wf_runtime_config runtime_config;

#define INOT_BUF_SIZE (1024 * sizeof(inotify_event))
static char buf[INOT_BUF_SIZE];

static std::string config_dir, config_file;

static void reload_config(int fd)
{
    wf::config::load_configuration_options_from_file(
        wf::get_core().config, config_file);
    inotify_add_watch(fd, config_dir.c_str(), IN_CREATE);
    inotify_add_watch(fd, config_file.c_str(), IN_MODIFY);
}

static int handle_config_updated(int fd, uint32_t mask, void *data)
{
    LOGD("Reloading configuration file");

    /* read, but don't use */
    read(fd, buf, INOT_BUF_SIZE);
    reload_config(fd);

    wf::get_core().emit_signal("reload-config", nullptr);

    return 0;
}

static void print_version()
{
    std::cout << WAYFIRE_VERSION << std::endl;
    exit(0);
}

static void print_help()
{
    std::cout << "Wayfire " << WAYFIRE_VERSION << std::endl;
    std::cout << "Usage: wayfire [OPTION]...\n" << std::endl;
    std::cout << " -c,  --config            specify config file to use" << std::endl;
    std::cout << " -h,  --help              print this help" << std::endl;
    std::cout << " -d,  --debug             enable debug logging" << std::endl;
    std::cout <<
        " -D,  --damage-debug      enable additional debug for damaged regions" <<
        std::endl;
    std::cout << " -R,  --damage-rerender   rerender damaged regions" << std::endl;
    std::cout << " -v,  --version           print version and exit" << std::endl;
    exit(0);
}

std::map<EGLint, EGLint> default_attribs = {
    {EGL_RED_SIZE, 1},
    {EGL_GREEN_SIZE, 1},
    {EGL_BLUE_SIZE, 1},
    {EGL_DEPTH_SIZE, 1},
};

std::map<wlr_renderer*, wlr_egl*> egl_for_renderer;

/* Merge the default config and the config we need */
static std::vector<EGLint> generate_config_attribs(EGLint *renderer_attribs)
{
    std::vector<EGLint> attribs;

    /* See what we have in the default config */
    for (auto i = renderer_attribs; i != NULL && *i != EGL_NONE; i++)
    {
        /* We will override this value later */
        if (default_attribs.count(*i))
        {
            ++i;
            continue;
        }

        attribs.push_back(*i);
        i++;
        attribs.push_back(*i);
    }

    /* Then pack all values we want */
    for (auto & p : default_attribs)
    {
        attribs.push_back(p.first);
        attribs.push_back(p.second);
    }

    attribs.push_back(EGL_NONE);

    return attribs;
}

wlr_renderer *add_egl_depth_renderer(wlr_egl *egl, EGLenum platform,
    void *remote, EGLint *_r_attr, EGLint visual)
{
    bool r;
    auto attribs = generate_config_attribs(_r_attr);
    r = wlr_egl_init(egl, platform, remote, attribs.data(), visual);

    if (!r)
    {
        LOGE("Failed to initialize EGL");

        return NULL;
    }

    auto renderer = wlr_gles2_renderer_create(egl);
    if (!renderer)
    {
        LOGE("Failed to create GLES2 renderer");
        wlr_egl_finish(egl);

        return NULL;
    }

    egl_for_renderer[renderer] = egl;

    return renderer;
}

namespace wf
{
namespace _safe_list_detail
{
wl_event_loop *event_loop;
void idle_cleanup_func(void *data)
{
    auto priv = reinterpret_cast<std::function<void()>*>(data);
    (*priv)();
}
}
}

static bool drop_permissions(void)
{
    if ((getuid() != geteuid()) || (getgid() != getegid()))
    {
        // Set the gid and uid in the correct order.
        if ((setgid(getgid()) != 0) || (setuid(getuid()) != 0))
        {
            LOGE("Unable to drop root, refusing to start");

            return false;
        }
    }

    if ((setgid(0) != -1) || (setuid(0) != -1))
    {
        LOGE("Unable to drop root (we shouldn't be able to "
             "restore it after setuid), refusing to start");

        return false;
    }

    return true;
}

static wf::log::color_mode_t detect_color_mode()
{
    return isatty(STDOUT_FILENO) ?
           wf::log::LOG_COLOR_MODE_ON : wf::log::LOG_COLOR_MODE_OFF;
}

static void wlr_log_handler(wlr_log_importance level,
    const char *fmt, va_list args)
{
    const int bufsize = 4 * 1024;
    char buffer[bufsize];
    vsnprintf(buffer, bufsize, fmt, args);

    wf::log::log_level_t wlevel;
    switch (level)
    {
      case WLR_ERROR:
        wlevel = wf::log::LOG_LEVEL_ERROR;
        break;

      case WLR_INFO:
        wlevel = wf::log::LOG_LEVEL_INFO;
        break;

      case WLR_DEBUG:
        wlevel = wf::log::LOG_LEVEL_DEBUG;
        break;

      default:
        return;
    }

    wf::log::log_plain(wlevel, buffer);
}

#ifndef ASAN_ENABLED
static void signal_handler(int signal)
{
    std::string error;
    switch (signal)
    {
      case SIGSEGV:
        error = "Segmentation fault";
        break;

      case SIGFPE:
        error = "Floating-point exception";
        break;

      case SIGABRT:
        error = "Fatal error(SIGABRT)";
        break;

      default:
        error = "Unknown";
    }

    LOGE("Fatal error: ", error);
    wf::print_trace(false);
    std::exit(0);
}

#endif

int main(int argc, char *argv[])
{
    config_dir = nonull(getenv("XDG_CONFIG_HOME"));
    if (!config_dir.compare("nil"))
    {
        config_dir = std::string(nonull(getenv("HOME"))) + "/.config";
    }

    config_file = config_dir + "/wayfire.ini";

    wf::log::log_level_t log_level = wf::log::LOG_LEVEL_INFO;
    struct option opts[] = {
        {
            "config", required_argument, NULL, 'c'
        },
        {"debug", no_argument, NULL, 'd'},
        {"damage-debug", no_argument, NULL, 'D'},
        {"damage-rerender", no_argument, NULL, 'R'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'v'},
        {0, 0, NULL, 0}
    };

    int c, i;
    while ((c = getopt_long(argc, argv, "c:dDhRv", opts, &i)) != -1)
    {
        switch (c)
        {
          case 'c':
            config_file = optarg;
            break;

          case 'D':
            runtime_config.damage_debug = true;
            break;

          case 'R':
            runtime_config.no_damage_track = true;
            break;

          case 'h':
            print_help();
            break;

          case 'd':
            log_level = wf::log::LOG_LEVEL_DEBUG;
            break;

          case 'v':
            print_version();
            break;

          default:
            std::cerr << "Unrecognized command line argument " << optarg << "\n" <<
                std::endl;
        }
    }

    auto wlr_log_level =
        (log_level == wf::log::LOG_LEVEL_DEBUG ? WLR_DEBUG : WLR_ERROR);
    wlr_log_init(wlr_log_level, wlr_log_handler);
    wf::log::initialize_logging(std::cout, log_level, detect_color_mode());

#ifndef ASAN_ENABLED
    /* In case of crash, print the stacktrace for debugging.
     * However, if ASAN is enabled, we'll get better stacktrace from there. */
    signal(SIGSEGV, signal_handler);
    signal(SIGFPE, signal_handler);
    signal(SIGABRT, signal_handler);
#endif

    LOGI("Starting wayfire version ", WAYFIRE_VERSION);
    /* First create display and initialize safe-list's event loop, so that
     * wf objects (which depend on safe-list) can work */
    auto display = wl_display_create();
    wf::_safe_list_detail::event_loop = wl_display_get_event_loop(display);

    auto& core = wf::get_core_impl();

    /** TODO: move this to core_impl constructor */
    core.display  = display;
    core.ev_loop  = wl_display_get_event_loop(core.display);
    core.backend  = wlr_backend_autocreate(core.display, add_egl_depth_renderer);
    core.renderer = wlr_backend_get_renderer(core.backend);
    core.egl = egl_for_renderer[core.renderer];
    assert(core.egl);

    if (!drop_permissions())
    {
        wl_display_destroy_clients(core.display);
        wl_display_destroy(core.display);

        return EXIT_FAILURE;
    }

    std::vector<std::string> xmldirs;
    if (char *plugin_xml_path = getenv("WAYFIRE_PLUGIN_XML_PATH"))
    {
        std::stringstream ss(plugin_xml_path);
        std::string entry;
        while (std::getline(ss, entry, ':'))
        {
            xmldirs.push_back(entry);
        }
    }

    xmldirs.push_back(PLUGIN_XML_DIR);

    LOGI("using config file: ", config_file.c_str());
    core.config = wf::config::build_configuration(
        xmldirs, SYSCONFDIR "/wayfire/defaults.ini", config_file);

    int inotify_fd = inotify_init1(IN_CLOEXEC);
    reload_config(inotify_fd);

    wl_event_loop_add_fd(core.ev_loop, inotify_fd, WL_EVENT_READABLE,
        handle_config_updated, NULL);
    core.init();

    auto server_name = wl_display_add_socket_auto(core.display);
    if (!server_name)
    {
        LOGE("failed to create wayland, socket, exiting");

        return -1;
    }

    setenv("_WAYLAND_DISPLAY", server_name, 1);

    core.wayland_display = server_name;
    if (!wlr_backend_start(core.backend))
    {
        LOGE("failed to initialize backend, exiting");
        wlr_backend_destroy(core.backend);
        wl_display_destroy(core.display);

        return -1;
    }

    LOGI("running at server ", server_name);
    setenv("WAYLAND_DISPLAY", server_name, 1);
    wf::xwayland_set_seat(core.get_current_seat());
    wl_display_run(core.display);

    /* Teardown */
    wl_display_destroy_clients(core.display);
    wl_display_destroy(core.display);

    return EXIT_SUCCESS;
}
