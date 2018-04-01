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
class wayfire_xdg6_popup;
struct desktop_apis_t
{
    wlr_xdg_shell_v6 *v6;
    wlr_xwayland *xwayland;

    std::map<wlr_xdg_surface_v6*, wayfire_xdg6_popup*> xdg_v6_popups;
    wl_listener v6_created, xwayland_created;
};

void init_desktop_apis();

#endif /* end of include guard: DESKTOP_API_HPP */
