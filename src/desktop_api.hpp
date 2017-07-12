#ifndef DESKTOP_API_HPP
#define DESKTOP_API_HPP

#include <compositor.h>
#include <libweston-desktop.h>

#include "commonincludes.hpp"
#include "core.hpp"
#include "output.hpp"
#include "signal_definitions.hpp"

void desktop_surface_added(weston_desktop_surface *desktop_surface, void *shell)
{
    debug << "desktop_surface_added" << std::endl;
    core->add_view(desktop_surface);
}

void desktop_surface_removed(weston_desktop_surface *surface, void *user_data)
{
    debug << "desktop_surface_removed" << std::endl;

    auto view = core->find_view(surface);
    core->erase_view(view);

    weston_desktop_surface_unlink_view(view->handle);
    view->destroyed = true;
    view->output->detach_view(view);

    if (view->keep_count <= 0) /* plugins might want to keep this */
        weston_view_destroy(view->handle);
}

void desktop_surface_commited (weston_desktop_surface *desktop_surface,
        int32_t sx, int32_t sy, void *data)
{
    auto view = core->find_view(desktop_surface);
    assert(view != nullptr);

    if (view->surface->width == 0) {
        return;
    }

    view->map(sx, sy);
}

void desktop_surface_set_xwayland_position(weston_desktop_surface *desktop_surface,
        int32_t x, int32_t y, void *shell)
{
    auto view = core->find_view(desktop_surface);
    assert(view != nullptr);

    view->xwayland.is_xorg = true;
    view->xwayland.x = x;
    view->xwayland.y = y;
}

void desktop_surface_move(weston_desktop_surface *ds, weston_seat *seat,
        uint32_t serial, void *shell)
{
    auto view = core->find_view(ds);
    auto ptr = weston_seat_get_pointer(seat);

    if (ptr && ptr->focus && ptr->button_count > 0 && ptr->grab_serial == serial) {
        auto main_surface = weston_surface_get_main_surface(view->surface);
        if (main_surface == view->surface) {
            auto req = new move_request_signal;
            req->ptr = ptr;
            view->output->signal->emit_signal("move-request", req);
            delete req;
        }
    }
}
void desktop_surface_resize(weston_desktop_surface *ds, weston_seat *seat,
        uint32_t serial, weston_desktop_surface_edge edges, void *shell)
{
    auto view = core->find_view(ds);
    auto ptr = weston_seat_get_pointer(seat);

    if (ptr && ptr->focus && ptr->button_count > 0 && ptr->grab_serial == serial) {
        auto main_surface = weston_surface_get_main_surface(view->surface);
        if (main_surface == view->surface) {
            auto req = new resize_request_signal;
            req->ptr = ptr;
            req->edges = edges;
            view->output->signal->emit_signal("resize-request", req);
            delete req;
        }
    }
}

void desktop_surface_maximized_requested(weston_desktop_surface *ds,
        bool maximized, void *shell)
{
    auto view = core->find_view(ds);
    if (!view || view->maximized == maximized)
        return;

    view->set_maximized(maximized);

    if (view->is_mapped) {
        view_maximized_signal data;
        data.view = view;
        data.state = maximized;

        view->output->signal->emit_signal("view-maximized-request", &data);
    } else {
        view->set_geometry(view->output->workspace->get_workarea());
    }
}

void desktop_surface_fullscreen_requested(weston_desktop_surface *ds,
        bool full, weston_output *output, void *shell)
{
    auto view = core->find_view(ds);
    if (!view || view->fullscreen == full)
        return;

    auto wo = (output ? core->get_output(output) : view->output);
    assert(wo);

    if (view->output != wo)
        core->move_view_to_output(view, view->output, wo);

    view->set_fullscreen(full);

    if (view->is_mapped) {
        view_fullscreen_signal data;
        data.view = view;
        data.state = full;

        wo->signal->emit_signal("view-fullscreen-request", &data);
    } else {
        view->set_geometry(view->output->get_full_geometry());
    }
}

#endif /* end of include guard: DESKTOP_API_HPP */
