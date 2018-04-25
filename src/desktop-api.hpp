#ifndef DESKTOP_API_HPP
#define DESKTOP_API_HPP

extern "C"
{
#include <wlr/types/wlr_xdg_shell_v6.h>

#define class class_t
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/xwayland.h>
#undef class
}

#include <map>

/* TODO: do we really need to implement wl_shell? who is using it nowadays? */
class wayfire_surface_t;
class decorator_base_t;

struct desktop_apis_t
{
    wlr_xdg_shell_v6 *v6;
    wlr_xwayland *xwayland;

    decorator_base_t *decorator = NULL;

    std::map<wlr_surface *, wayfire_surface_t *> desktop_surfaces;
    wl_listener v6_created, xwayland_created, xwayland_mapped;
};

void init_desktop_apis();

#endif /* end of include guard: DESKTOP_API_HPP */
