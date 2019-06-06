#include "debug.hpp"
#include "core.hpp"
#include "surface-impl.hpp"
#include "output.hpp"
#include "decorator.hpp"
#include "xdg-shell.hpp"
#include "output-layout.hpp"

template<class XdgPopupVersion>
wayfire_xdg_popup<XdgPopupVersion>::wayfire_xdg_popup(XdgPopupVersion *popup)
    : wlr_child_surface_base_t(wf::wf_surface_from_void(popup->parent->data), this)
{
    assert(priv->parent_surface);
    this->popup = popup;

    on_map.set_callback([&] (void*) { map(this->popup->base->surface); });
    on_unmap.set_callback([&] (void*) { unmap(); });
    on_destroy.set_callback([&] (void*) { unref(); });
    on_new_popup.set_callback([&] (void* data) {
        create_xdg_popup((XdgPopupVersion*) data);
    });

    on_map.connect(&popup->base->events.map);
    on_unmap.connect(&popup->base->events.unmap);
    on_destroy.connect(&popup->base->events.destroy);
    on_new_popup.connect(&popup->base->events.new_popup);

    popup->base->data = this;
    unconstrain();
}

template<class XdgPopupVersion>
void wayfire_xdg_popup<XdgPopupVersion>::unconstrain()
{
    auto view = dynamic_cast<wf::view_interface_t*> (get_main_surface());
    if (!get_output() || !view)
        return;

    auto box = get_output()->get_relative_geometry();
    auto wm = view->get_output_geometry();
    box.x -= wm.x;
    box.y -= wm.y;

    _do_unconstrain(box);
}

template<>
void wayfire_xdg_popup<wlr_xdg_popup>::_do_unconstrain(wlr_box box) {
    wlr_xdg_popup_unconstrain_from_box(popup, &box);
}

template<>
void wayfire_xdg_popup<wlr_xdg_popup_v6>::_do_unconstrain(wlr_box box) {
    wlr_xdg_popup_v6_unconstrain_from_box(popup, &box);
}

template<class XdgPopupVersion>
wayfire_xdg_popup<XdgPopupVersion>::~wayfire_xdg_popup()
{
    on_map.disconnect();
    on_unmap.disconnect();
    on_destroy.disconnect();
    on_new_popup.disconnect();
}

template<class XdgPopupVersion>
wf_point wayfire_xdg_popup<XdgPopupVersion>::get_window_offset()
{
    return {
        popup->base->geometry.x,
        popup->base->geometry.y,
    };
}

template<class XdgPopupVersion>
wf_point wayfire_xdg_popup<XdgPopupVersion>::get_offset()
{
    auto parent = dynamic_cast<wf::wlr_surface_base_t*>(
        wf::wf_surface_from_void(popup->parent->data));
    assert(parent);

    wf_point popup_offset = {
        popup->geometry.x,
        popup->geometry.y,
    };

    return (parent->get_window_offset() + popup_offset) + (-get_window_offset());
}

template<>
void wayfire_xdg_popup<wlr_xdg_popup_v6>::send_done()
{
    if (is_mapped())
        wlr_xdg_surface_v6_send_close(popup->base);
}

template<>
void wayfire_xdg_popup<wlr_xdg_popup>::send_done()
{
    if (is_mapped())
        wlr_xdg_popup_destroy(popup->base);
}

template<class XdgPopupVersion>
void create_xdg_popup_templ(XdgPopupVersion *popup)
{
    auto parent = wf::wf_surface_from_void(popup->parent->data);
    if (!parent)
    {
        log_error("attempting to create a popup with unknown parent");
        return;
    }

    new wayfire_xdg_popup<XdgPopupVersion>(popup);
}

template<class XdgPopupVersion>
void create_xdg_popup(XdgPopupVersion *popup) { create_xdg_popup_templ(popup); }

// specialized in header
template<> void create_xdg_popup<wlr_xdg_popup> (wlr_xdg_popup *popup) {
    create_xdg_popup_templ(popup);
}

template<class XdgToplevelVersion>
wayfire_xdg_view<XdgToplevelVersion>::wayfire_xdg_view(XdgToplevelVersion *top)
    : wf::wlr_view_t(), xdg_toplevel(top)
{
    log_info ("new xdg_shell_stable surface: %s app-id: %s",
              nonull(xdg_toplevel->title),
              nonull(xdg_toplevel->app_id));

    handle_title_changed(nonull(xdg_toplevel->title));
    handle_app_id_changed(nonull(xdg_toplevel->app_id));

    on_map.set_callback([&] (void*) { map(xdg_toplevel->base->surface); });
    on_unmap.set_callback([&] (void*) { unmap(); });
    on_destroy.set_callback([&] (void*) { destroy(); });
    on_new_popup.set_callback([&] (void* data) {
        create_xdg_popup((decltype(top->base->popup))data);
    });

    on_set_title.set_callback([&] (void*) {
        handle_title_changed(nonull(xdg_toplevel->title));
    });
    on_set_app_id.set_callback([&] (void*) {
        handle_app_id_changed(nonull(xdg_toplevel->app_id));
    });
    on_set_parent.set_callback([&] (void*) {
        auto parent = xdg_toplevel->parent ?
            wf::wf_view_from_void(xdg_toplevel->parent->data)->self() : nullptr;
        set_toplevel_parent(parent);
    });

    on_request_move.set_callback([&] (void*) { move_request(); });
    on_request_resize.set_callback([&] (void*) { resize_request(); });
    on_request_minimize.set_callback([&] (void*) { minimize_request(true); });
    on_request_maximize.set_callback([&] (void* data) {
        maximize_request(xdg_toplevel->client_pending.maximized);
    });
    on_request_fullscreen.set_callback([&] (void* data) {
        auto ev = static_cast<wlr_xdg_toplevel_set_fullscreen_event*> (data);
        auto wo = wf::get_core().output_layout->find_output(ev->output);
        fullscreen_request(wo, ev->fullscreen);
    });

    on_map.connect(&xdg_toplevel->base->events.map);
    on_unmap.connect(&xdg_toplevel->base->events.unmap);
    on_destroy.connect(&xdg_toplevel->base->events.destroy);
    on_new_popup.connect(&xdg_toplevel->base->events.new_popup);

    on_set_title.connect(&xdg_toplevel->events.set_title);
    on_set_app_id.connect(&xdg_toplevel->events.set_app_id);
    on_set_parent.connect(&xdg_toplevel->events.set_parent);
    on_request_move.connect(&xdg_toplevel->events.request_move);
    on_request_resize.connect(&xdg_toplevel->events.request_resize);
    on_request_maximize.connect(&xdg_toplevel->events.request_maximize);
    on_request_minimize.connect(&xdg_toplevel->events.request_minimize);
    on_request_fullscreen.connect(&xdg_toplevel->events.request_fullscreen);

    xdg_toplevel->base->data = dynamic_cast<view_interface_t*> (this);
    // set initial parent
    on_set_parent.emit(nullptr);
}

template<class XdgToplevelVersion>
wayfire_xdg_view<XdgToplevelVersion>::~wayfire_xdg_view() {}

template<class XdgToplevelVersion>
wf_geometry get_xdg_geometry(XdgToplevelVersion *toplevel);

template<>
wf_geometry get_xdg_geometry<wlr_xdg_toplevel>(wlr_xdg_toplevel *toplevel)
{
    wlr_box xdg_geometry;
    wlr_xdg_surface_get_geometry(toplevel->base, &xdg_geometry);
    return xdg_geometry;
}

template<>
wf_geometry get_xdg_geometry<wlr_xdg_toplevel_v6>(wlr_xdg_toplevel_v6 *toplevel)
{
    wlr_box xdg_geometry;
    wlr_xdg_surface_v6_get_geometry(toplevel->base, &xdg_geometry);
    return xdg_geometry;
}

template<class XdgToplevelVersion>
void wayfire_xdg_view<XdgToplevelVersion>::map(wlr_surface *surface)
{
    if (xdg_toplevel->client_pending.maximized)
        maximize_request(true);

    if (xdg_toplevel->client_pending.fullscreen)
        fullscreen_request(get_output(), true);

    wlr_view_t::map(surface);
    create_toplevel();
}

template<class XdgToplevelVersion>
void wayfire_xdg_view<XdgToplevelVersion>::commit()
{
    wlr_view_t::commit();

    /* On each commit, check whether the window geometry of the xdg_surface
     * changed. In those cases, we need to adjust the view's output geometry,
     * so that the apparent wm geometry doesn't change */
    auto wm = get_wm_geometry();
    auto xdg_g = get_xdg_geometry(xdg_toplevel);
    if (xdg_g.x != xdg_surface_offset.x || xdg_g.y != xdg_surface_offset.y)
    {
        xdg_surface_offset = {xdg_g.x, xdg_g.y};
        /* Note that we just changed the xdg_surface offset, which means we
         * also changed the wm geometry. Plugins which depend on the
         * geometry-changed signal however need to receive the appropriate
         * old geometry */
        set_position(wm.x, wm.y, wm, true);
    }
}

template<class XdgToplevelVersion>
wf_point wayfire_xdg_view<XdgToplevelVersion>::get_window_offset()
{
    return xdg_surface_offset;
}

template<class XdgToplevelVersion>
wf_geometry wayfire_xdg_view<XdgToplevelVersion>::get_wm_geometry()
{
    if (!is_mapped())
        return get_output_geometry();

    auto output_g = get_output_geometry();
    auto xdg_geometry = get_xdg_geometry(xdg_toplevel);

    wf_geometry wm = {
        output_g.x + xdg_surface_offset.x,
        output_g.y + xdg_surface_offset.y,
        xdg_geometry.width,
        xdg_geometry.height
    };

    if (view_impl->frame)
        wm = view_impl->frame->expand_wm_geometry(wm);

    return wm;
}

template<class XdgToplevelVersion>
void wayfire_xdg_view<XdgToplevelVersion>::set_activated(bool act)
{
    /* we don't send activated or deactivated for shell views,
     * they should always be active */
    if (this->role == wf::VIEW_ROLE_SHELL_VIEW)
        act = true;

    _set_activated(act);
    wf::wlr_view_t::set_activated(act);
}

template<>
void wayfire_xdg_view<wlr_xdg_toplevel>::_set_activated(bool act) {
    wlr_xdg_toplevel_set_activated(xdg_toplevel->base, act);
}

template<>
void wayfire_xdg_view<wlr_xdg_toplevel_v6>::_set_activated(bool act) {
    wlr_xdg_toplevel_v6_set_activated(xdg_toplevel->base, act);
}

template<>
void wayfire_xdg_view<wlr_xdg_toplevel>::set_tiled(uint32_t edges)
{
    wlr_xdg_toplevel_set_tiled(xdg_toplevel->base, edges);
    this->tiled_edges = edges;
}

template<>
void wayfire_xdg_view<wlr_xdg_toplevel_v6>::set_tiled(uint32_t edges) {
    /* Tiled is not supported in v6 */
    this->tiled_edges = edges;
}

template<>
void wayfire_xdg_view<wlr_xdg_toplevel>::set_maximized(bool max)
{
    wf::wlr_view_t::set_maximized(max);
    wlr_xdg_toplevel_set_maximized(xdg_toplevel->base, max);
}

template<>
void wayfire_xdg_view<wlr_xdg_toplevel_v6>::set_maximized(bool max)
{
    wf::wlr_view_t::set_maximized(max);
    wlr_xdg_toplevel_v6_set_maximized(xdg_toplevel->base, max);
}

template<>
void wayfire_xdg_view<wlr_xdg_toplevel>::set_fullscreen(bool full)
{
    wf::wlr_view_t::set_fullscreen(full);
    wlr_xdg_toplevel_set_fullscreen(xdg_toplevel->base, full);
}

template<>
void wayfire_xdg_view<wlr_xdg_toplevel_v6>::set_fullscreen(bool full)
{
    wf::wlr_view_t::set_fullscreen(full);
    wlr_xdg_toplevel_v6_set_fullscreen(xdg_toplevel->base, full);
}

template<>
void wayfire_xdg_view<wlr_xdg_toplevel>::resize(int w, int h)
{
    if (view_impl->frame)
        view_impl->frame->calculate_resize_size(w, h);
    wlr_xdg_toplevel_set_size(xdg_toplevel->base, w, h);
}

template<>
void wayfire_xdg_view<wlr_xdg_toplevel_v6>::resize(int w, int h)
{
    if (view_impl->frame)
        view_impl->frame->calculate_resize_size(w, h);
    wlr_xdg_toplevel_v6_set_size(xdg_toplevel->base, w, h);
}

template<>
void wayfire_xdg_view<wlr_xdg_toplevel>::request_native_size() {
    wlr_xdg_toplevel_set_size(xdg_toplevel->base, 0, 0);
}

template<>
void wayfire_xdg_view<wlr_xdg_toplevel_v6>::request_native_size() {
    wlr_xdg_toplevel_v6_set_size(xdg_toplevel->base, 0, 0);
}

template<>
void wayfire_xdg_view<wlr_xdg_toplevel>::close() {
    wlr_xdg_toplevel_send_close(xdg_toplevel->base);
    wf::wlr_view_t::close();
}

template<>
void wayfire_xdg_view<wlr_xdg_toplevel_v6>::close() {
    wlr_xdg_surface_v6_send_close(xdg_toplevel->base);
    wf::wlr_view_t::close();
}

template<class XdgToplevelVersion>
void wayfire_xdg_view<XdgToplevelVersion>::destroy()
{
    on_map.disconnect();
    on_unmap.disconnect();
    on_destroy.disconnect();
    on_new_popup.disconnect();
    on_set_title.disconnect();
    on_set_app_id.disconnect();
    on_set_parent.disconnect();
    on_request_move.disconnect();
    on_request_resize.disconnect();
    on_request_maximize.disconnect();
    on_request_minimize.disconnect();
    on_request_fullscreen.disconnect();

    xdg_toplevel = nullptr;
    wf::wlr_view_t::destroy();
}

static wlr_xdg_shell *xdg_handle;
static wlr_xdg_shell_v6 *xdg_handle_v6;


void wf::init_xdg_shell()
{
    static wf::wl_listener_wrapper on_xdg_created, on_xdg6_created;
    xdg_handle = wlr_xdg_shell_create(wf::get_core().display);

    if (xdg_handle)
    {
        on_xdg_created.set_callback([&] (void *data) {
            auto surf = static_cast<wlr_xdg_surface*> (data);
            if (surf->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
            {
                wf::get_core().add_view(
                    std::make_unique<wayfire_xdg_view<wlr_xdg_toplevel>> (
                        surf->toplevel));
            }
        });
        on_xdg_created.connect(&xdg_handle->events.new_surface);
    }

    xdg_handle_v6 = wlr_xdg_shell_v6_create(wf::get_core().display);
    if (xdg_handle_v6)
    {
        on_xdg6_created.set_callback([&] (void *data) {
            auto surf = static_cast<wlr_xdg_surface_v6*> (data);
            if (surf->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL)
            {
                wf::get_core().add_view(
                    std::make_unique<wayfire_xdg_view<wlr_xdg_toplevel_v6>> (
                        surf->toplevel));
            }
        });
        on_xdg6_created.connect(&xdg_handle_v6->events.new_surface);
    }
}

template class wayfire_xdg_popup<wlr_xdg_popup_v6>;
template class wayfire_xdg_popup<wlr_xdg_popup>;
template class wayfire_xdg_view<wlr_xdg_toplevel_v6>;
template class wayfire_xdg_view<wlr_xdg_toplevel>;
