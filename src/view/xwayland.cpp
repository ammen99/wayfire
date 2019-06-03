#include "debug.hpp"
#include "core.hpp"
#include "output.hpp"
#include "workspace-manager.hpp"
#include "decorator.hpp"
#include "output-layout.hpp"
#include "../core/core-impl.hpp"
#include "view-impl.hpp"

extern "C"
{
#include <wlr/config.h>

#if WLR_HAS_XWAYLAND
#define class class_t
#define static
#include <wlr/xwayland.h>
#undef static
#undef class
#endif
}

#if WLR_HAS_XWAYLAND

class wayfire_xwayland_view_base : public wf::wlr_view_t
{
    protected:
    wf::wl_listener_wrapper on_destroy, on_unmap, on_map, on_configure,
        on_set_title, on_set_app_id;

    wlr_xwayland_surface *xw;
    int last_server_width = 0;
    int last_server_height = 0;

    wf::signal_callback_t output_geometry_changed =
        [this] (wf::signal_data_t*)
    {
        if (is_mapped())
            move(geometry.x, geometry.y);
    };

    public:

    wayfire_xwayland_view_base(wlr_xwayland_surface *xww)
        : wlr_view_t(), xw(xww)
    {
        on_map.set_callback([&] (void*) { map(xw->surface); });
        on_unmap.set_callback([&] (void*) { unmap(); });
        on_destroy.set_callback([&] (void*) { destroy(); });
        on_configure.set_callback([&] (void* data) {
            auto ev = static_cast<wlr_xwayland_surface_configure_event*> (data);
            configure_request({ev->x, ev->y, ev->width, ev->height});
        });
        on_set_title.set_callback([&] (void*) {
            handle_title_changed(nonull(xw->title));
        });
        on_set_app_id.set_callback([&] (void*) {
            handle_app_id_changed(nonull(xw->class_t));
        });

        handle_title_changed(nonull(xw->title));
        handle_app_id_changed(nonull(xw->class_t));

        on_map.connect(&xw->events.map);
        on_unmap.connect(&xw->events.unmap);
        on_destroy.connect(&xw->events.destroy);
        on_configure.connect(&xw->events.request_configure);
        on_set_title.connect(&xw->events.set_title);
        on_set_app_id.connect(&xw->events.set_class);
    }

    virtual void destroy() override
    {
        if (get_output())
        {
            get_output()->disconnect_signal(
                "output-configuration-changed", &output_geometry_changed);
        }

        on_map.disconnect();
        on_unmap.disconnect();
        on_destroy.disconnect();
        on_configure.disconnect();
        on_set_title.disconnect();
        on_set_app_id.disconnect();

        wf::wlr_view_t::destroy();
    }

    virtual void configure_request(wf_geometry configure_geometry)
    {
        /* Wayfire positions views relative to their output, but Xwayland
         * windows have a global positioning. So, we need to make sure that we
         * always transform between output-local coordinates and global
         * coordinates */
        if (get_output())
        {
            auto og = get_output()->get_layout_geometry();
            configure_geometry.x -= og.x;
            configure_geometry.y -= og.y;

            /* It is possible the client requests to position itself on another
             * output. However, we want to make sure that the view stays on its
             * output, since this is how Wayland works */
            configure_geometry =
                clamp(configure_geometry, get_output()->get_relative_geometry());
        }

        if (view_impl->frame)
        {
            configure_geometry =
                view_impl->frame->expand_wm_geometry(configure_geometry);
        }

        set_geometry(configure_geometry);
    }

    virtual void close() override
    {
        wlr_xwayland_surface_close(xw);
        wf::wlr_view_t::close();
    }

    void send_configure(int width, int height)
    {
        if (!is_mapped())
            return;

        if (width < 0 || height < 0)
        {
            /* such a configure request would freeze xwayland.
             * This is most probably a bug somewhere in the compositor. */
            log_error("Configuring a xwayland surface with width/height <0");
            return;
        }

        auto output_geometry = get_output_geometry();

        int configure_x = output_geometry.x;
        int configure_y = output_geometry.y;

        if (get_output())
        {
            auto real_output = get_output()->get_layout_geometry();
            configure_x += real_output.x;
            configure_y += real_output.y;
        }

        wlr_xwayland_surface_configure(xw,
            configure_x, configure_y, width, height);
    }

    void send_configure()
    {
        send_configure(last_server_width, last_server_height);
    }

    void move(int x, int y) override
    {
        wf::wlr_view_t::move(x, y);
        if (!view_impl->in_continuous_move)
            send_configure();
    }

    virtual void set_output(wf::output_t *wo) override
    {
        if (get_output())
        {
            get_output()->disconnect_signal("output-configuration-changed",
                &output_geometry_changed);
        }

        wlr_view_t::set_output(wo);

        if (wo)
        {
            wo->connect_signal("output-configuration-changed",
                &output_geometry_changed);
        }
        /* Update the real position */
        send_configure();
    }
};

class wayfire_unmanaged_xwayland_view : public wayfire_xwayland_view_base
{
    public:
    wayfire_unmanaged_xwayland_view(wlr_xwayland_surface *xww);

    int global_x, global_y;

    void commit() override;
    void map(wlr_surface *surface)override;
    void unmap()override;

    ~wayfire_unmanaged_xwayland_view() { }
};

class wayfire_xwayland_view : public wayfire_xwayland_view_base
{
    wf::wl_listener_wrapper on_request_move, on_request_resize,
        on_request_maximize, on_request_fullscreen, on_set_parent;

  public:
    wayfire_xwayland_view(wlr_xwayland_surface *xww)
        : wayfire_xwayland_view_base(xww)
    {
        log_info("new xwayland surface %s class: %s instance: %s",
            nonull(xw->title), nonull(xw->class_t), nonull(xw->instance));

        on_request_move.set_callback([&] (void*) { move_request(); });
        on_request_resize.set_callback([&] (void*) { resize_request(); });
        on_request_maximize.set_callback([&] (void*) {
            maximize_request(xw->maximized_horz && xw->maximized_vert);
        });
        on_request_fullscreen.set_callback([&] (void*) {
            fullscreen_request(get_output(), xw->fullscreen);
        });

        on_set_parent.set_callback([&] (void*) {
            auto parent = xw->parent ?
                wf::wf_view_from_void(xw->parent->data)->self() : nullptr;
            set_toplevel_parent(parent);
        });

        on_set_parent.connect(&xw->events.set_parent);
        on_request_move.connect(&xw->events.request_move);
        on_request_resize.connect(&xw->events.request_resize);
        on_request_maximize.connect(&xw->events.request_maximize);
        on_request_fullscreen.connect(&xw->events.request_fullscreen);

        xw->data = dynamic_cast<wf::view_interface_t*> (this);
    }

    virtual void destroy() override
    {
        on_set_parent.disconnect();
        on_request_move.disconnect();
        on_request_resize.disconnect();
        on_request_maximize.disconnect();
        on_request_fullscreen.disconnect();

        wayfire_xwayland_view_base::destroy();
    }

    void map(wlr_surface *surface) override
    {
        /* override-redirect status changed between creation and MapNotify */
        if (xw->override_redirect)
        {
            /* Copy the xsurface in stack, since the destroy() will likely
             * delete this */
            auto xsurface = xw;
            destroy();

            auto view = std::make_unique<wayfire_unmanaged_xwayland_view> (xsurface);
            auto view_ptr = view.get();

            wf::get_core().add_view(std::move(view));
            view_ptr->map(xsurface->surface);
            return;
        }

        if (xw->maximized_horz && xw->maximized_vert)
            maximize_request(true);

        if (xw->fullscreen)
            fullscreen_request(get_output(), true);

        auto real_output = get_output()->get_layout_geometry();
        if (!maximized && !fullscreen && !parent)
        {
            int desired_x = xw->x - real_output.x;
            int desired_y = xw->y - real_output.y;
            move(desired_x, desired_y);
        }

        wf::wlr_view_t::map(surface);
        create_toplevel();
    }

    void commit() override
    {
        if (!xw->has_alpha)
        {
            pixman_region32_union_rect(
                &surface->opaque_region, &surface->opaque_region,
                0, 0, surface->current.width, surface->current.height);
        }

        wf::wlr_view_t::commit();

        /* Avoid loops where the client wants to have a certain size but the
         * compositor keeps trying to resize it */
        last_server_width = geometry.width;
        last_server_height = geometry.height;
    }

    virtual bool should_be_decorated() override
    {
        return !(xw->decorations &
            (WLR_XWAYLAND_SURFACE_DECORATIONS_NO_TITLE |
             WLR_XWAYLAND_SURFACE_DECORATIONS_NO_BORDER));
    }

    void set_activated(bool active) override
    {
        wlr_xwayland_surface_activate(xw, active);
        wf::wlr_view_t::set_activated(active);
    }

    void set_moving(bool moving) override
    {
        wf::wlr_view_t::set_moving(moving);

        /* We don't send updates while in continuous move, because that means
         * too much configure requests. Instead, we set it at the end */
        if (!view_impl->in_continuous_move)
            send_configure();
    }

    void resize(int w, int h) override
    {
        if (view_impl->frame)
            view_impl->frame->calculate_resize_size(w, h);

        last_server_width = w;
        last_server_height = h;
        send_configure(w, h);
    }

    virtual void request_native_size() override
    {
        if (!is_mapped())
            return;

        if (xw->size_hints->base_width > 0 && xw->size_hints->base_height > 0)
        {
            last_server_width = xw->size_hints->base_width;
            last_server_height = xw->size_hints->base_height;
            send_configure();
        }
    }

    void set_maximized(bool maxim) override
    {
        wf::wlr_view_t::set_maximized(maxim);
        wlr_xwayland_surface_set_maximized(xw, maxim);
    }

    virtual void toplevel_send_app_id() override
    {
        if (!toplevel_handle)
            return;

        /* Xwayland windows have two "app-id"s - the class and the instance.
         * Some apps' icons can be found by looking up the class, for others
         * the instance. So, just like the workaround for gtk-shell, we can
         * send both the instance and the class to clients, so that they can
         * find the appropriate icons. */
        std::string app_id;
        auto default_app_id = get_app_id();
        auto instance_app_id = nonull(xw->instance);

        auto app_id_mode = (*wf::get_core().config)["workarounds"]
            ->get_option("app_id_mode", "stock");

        if (app_id_mode->as_string() == "full") {
            app_id = default_app_id + " " + instance_app_id;
        } else {
            app_id = default_app_id;
        }

        wlr_foreign_toplevel_handle_v1_set_app_id(
            toplevel_handle, app_id.c_str());
    }

    void set_fullscreen(bool full) override
    {
        wf::wlr_view_t::set_fullscreen(full);
        wlr_xwayland_surface_set_fullscreen(xw, full);
    }
};

wayfire_unmanaged_xwayland_view::
wayfire_unmanaged_xwayland_view(wlr_xwayland_surface *xww)
    : wayfire_xwayland_view_base(xww)
{
    log_info("new unmanaged xwayland surface %s class: %s instance: %s",
             nonull(xw->title), nonull(xw->class_t), nonull(xw->instance));

    xw->data = this;
    role = wf::VIEW_ROLE_UNMANAGED;
}

void wayfire_unmanaged_xwayland_view::commit()
{
    /* Xwayland O-R views manage their position on their own. So we need to
     * update their position on each commit, if the position changed. */
    if (global_x != xw->x || global_y != xw->y)
    {
        geometry.x = global_x = xw->x;
        geometry.y = global_y = xw->y;

        if (get_output())
        {
            auto real_output = get_output()->get_layout_geometry();
            geometry.x -= real_output.x;
            geometry.y -= real_output.y;
        }

        wf::wlr_view_t::move(geometry.x, geometry.y);
    }

    wlr_view_t::commit();
}

void wayfire_unmanaged_xwayland_view::map(wlr_surface *surface)
{
    /* move to the output where our center is
     * FIXME: this is a bad idea, because a dropdown menu might get sent to
     * an incorrect output. However, no matter how we calculate the real
     * output, we just can't be 100% compatible because in X all windows are
     * positioned in a global coordinate space */
    auto wo = wf::get_core().output_layout->get_output_at(
        xw->x + surface->current.width / 2, xw->y + surface->current.height / 2);

    if (!wo)
    {
        /* if surface center is outside of anything, try to check the output
         * where the pointer is */
        GetTuple(cx, cy, wf::get_core().get_cursor_position());
        wo = wf::get_core().output_layout->get_output_at(cx, cy);
    }

    if (!wo)
        wo = wf::get_core().get_active_output();
    assert(wo);


    auto real_output_geometry = wo->get_layout_geometry();

    global_x = xw->x;
    global_y = xw->y;
    wf::wlr_view_t::move(xw->x - real_output_geometry.x,
        xw->y - real_output_geometry.y);

    if (wo != get_output())
    {
        if (get_output())
            get_output()->workspace->remove_view(self());

        set_output(wo);
    }

    damage();

    /* We update the keyboard focus before emitting the map event, so that
     * plugins can detect that this view can have keyboard focus */
    view_impl->keyboard_focus_enabled = wlr_xwayland_or_surface_wants_focus(xw);

    get_output()->workspace->add_view(self(), wf::LAYER_XWAYLAND);
    wf::wlr_view_t::map(surface);

    if (wlr_xwayland_or_surface_wants_focus(xw))
    {
        /* Clients that need to interact with the user, just make sure that
         * they are not below a panel or similar */
        auto wa = get_output()->workspace->get_workarea();
        move(xw->x + wa.x - real_output_geometry.x,
            xw->y + wa.y - real_output_geometry.y);

        /* And focus them, since they are a new window */
        get_output()->focus_view(self());
    }
}

void wayfire_unmanaged_xwayland_view::unmap()
{
    /* O-R focuseable views are treated like normal windows, i.e they have the
     * pre-unmap event and have unmap animations */
    if (view_impl->keyboard_focus_enabled)
        emit_view_pre_unmap(self());

    wf::wlr_view_t::unmap();
}

static wlr_xwayland *xwayland_handle = nullptr;
#endif

void wf::init_xwayland()
{
#if WLR_HAS_XWAYLAND
    static wf::wl_listener_wrapper on_created;
    static signal_callback_t on_shutdown = [&] (void*) {
        wlr_xwayland_destroy(xwayland_handle);
    };

    on_created.set_callback([] (void *data) {
        auto xsurf = (wlr_xwayland_surface*) data;
        if (xsurf->override_redirect)
        {
            wf::get_core().add_view(
                std::make_unique<wayfire_unmanaged_xwayland_view>(xsurf));
        }
        else
        {
            wf::get_core().add_view(
                std::make_unique<wayfire_xwayland_view> (xsurf));
        }
    });

    xwayland_handle = wlr_xwayland_create(wf::get_core().display,
        wf::get_core_impl().compositor, false);
    if (xwayland_handle)
    {
        on_created.connect(&xwayland_handle->events.new_surface);
        wf::get_core().connect_signal("shutdown", &on_shutdown);
    }
#endif
}

void wf::xwayland_set_seat(wlr_seat *seat)
{
#if WLR_HAS_XWAYLAND
    if (xwayland_handle)
    {
        wlr_xwayland_set_seat(xwayland_handle,
            wf::get_core().get_current_seat());
    }
#endif
}

std::string wf::xwayland_get_display()
{
#if WLR_HAS_XWAYLAND
    return std::to_string(xwayland_handle ? xwayland_handle->display : -1);
#else
    return "-1";
#endif
}
