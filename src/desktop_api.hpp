#ifndef DESKTOP_API_HPP
#define DESKTOP_API_HPP

#include <libweston-1/compositor.h>
#include <libweston-1/libweston-desktop.h>

#include "commonincludes.hpp"
#include "core.hpp"

void desktop_surface_added(weston_desktop_surface *desktop_surface, void *shell) {
    debug << "desktop_surface_added" << std::endl;
    core->add_view(desktop_surface);

}
void desktop_surface_removed(weston_desktop_surface *surface, void *user_data) {
    /* TODO: what do we do when a view is destroyed ? */
}

void surface_commited () {}



#endif /* end of include guard: DESKTOP_API_HPP */
