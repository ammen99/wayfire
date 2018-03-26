#ifndef DESKTOP_API_HPP
#define DESKTOP_API_HPP

extern "C"
{
#include <wlr/types/wlr_xdg_shell_v6.h>
}

struct desktop_apis_t
{
    wlr_xdg_shell_v6 *v6;
    wl_listener v6_created;
};

void init_desktop_apis();

#endif /* end of include guard: DESKTOP_API_HPP */
