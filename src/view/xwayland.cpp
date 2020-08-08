#include "wayfire/debug.hpp"
#include <wayfire/util/log.hpp>
#include "wayfire/core.hpp"
#include "wayfire/output.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wayfire/decorator.hpp"
#include "wayfire/output-layout.hpp"
#include "wayfire/signal-definitions.hpp"
#include "../core/core-impl.hpp"
#include "view-impl.hpp"

extern "C"
{
#include <wlr/config.h>

#if WF_HAS_XWAYLAND
 #define class class_t
 #define static
 #include <wlr/xwayland.h>
 #include <wlr/xcursor.h>
 #undef static
 #undef class
#endif
}

#if WF_HAS_XWAYLAND

class wayfire_xwayland_view_base : public wf::wlr_view_t
{
  protected:
    static xcb_atom_t _NET_WM_WINDOW_TYPE_NORMAL;

  public:
    static bool load_atoms()
    {
        auto connection = xcb_connect(NULL, NULL);
        if (!connection || xcb_connection_has_error(connection))
        {
            return false;
        }

        const std::string name = "_NET_WM_WINDOW_TYPE_NORMAL";
        auto cookie = xcb_intern_atom(connection, 0, name.length(), name.c_str());

        xcb_generic_error_t *error = NULL;
        xcb_intern_atom_reply_t *reply;
        reply = xcb_intern_atom_reply(connection, cookie, &error);

        bool success = !error && reply;
        if (success)
        {
            _NET_WM_WINDOW_TYPE_NORMAL = reply->atom;
        }

        free(reply);
        free(error);

        xcb_disconnect(connection);

        return true;
    }

  protected:
    wf::wl_listener_wrapper on_destroy, on_unmap, on_map, on_configure,
        on_set_title, on_set_app_id;

    wlr_xwayland_surface *xw;
    /** The geometry requested by the client */
    bool self_positioned = false;

    wf::signal_connection_t output_geometry_changed{[this] (wf::signal_data_t*)
        {
            if (is_mapped())
            {
                auto wm_geometry = get_wm_geometry();
                move(wm_geometry.x, wm_geometry.y);
            }
        }
    };

  public:
    wayfire_xwayland_view_base(wlr_xwayland_surface *xww) :
        wlr_view_t(), xw(xww)
    {}

    virtual void initialize() override
    {
        wf::wlr_view_t::initialize();
        on_map.set_callback([&] (void*) { map(xw->surface); });
        on_unmap.set_callback([&] (void*) { unmap(); });
        on_destroy.set_callback([&] (void*) { destroy(); });
        on_configure.set_callback([&] (void *data)
        {
            auto ev = static_cast<wlr_xwayland_surface_configure_event*>(data);
            wf::point_t output_origin = {0, 0};
            if (get_output())
            {
                output_origin = {
                    get_output()->get_relative_geometry().x,
                    get_output()->get_relative_geometry().y
                };
            }

            if (!is_mapped())
            {
                /* If the view is not mapped yet, let it be configured as it
                 * wishes. We will position it properly in ::map() */
                wlr_xwayland_surface_configure(xw,
                    ev->x, ev->y, ev->width, ev->height);

                if ((ev->mask & XCB_CONFIG_WINDOW_X) &&
                    (ev->mask & XCB_CONFIG_WINDOW_Y))
                {
                    this->self_positioned = true;
                    this->geometry.x = ev->x - output_origin.x;
                    this->geometry.y = ev->y - output_origin.y;
                }

                return;
            }

            /**
             * Regular Xwayland windows are not allowed to change their position
             * after mapping, in which respect they behave just like Wayland apps.
             *
             * However, OR views or special views which do not have NORMAL type
             * should be allowed to move around the screen.
             */
            bool enable_custom_position = xw->override_redirect ||
                (xw->window_type_len > 0 &&
                    xw->window_type[0] != _NET_WM_WINDOW_TYPE_NORMAL);

            if ((ev->mask & XCB_CONFIG_WINDOW_X) &&
                (ev->mask & XCB_CONFIG_WINDOW_Y) &&
                enable_custom_position)
            {
                /* override-redirect views generally have full freedom. */
                self_positioned = true;
                configure_request({ev->x, ev->y, ev->width, ev->height});

                return;
            }

            /* Use old x/y values */
            ev->x = geometry.x + output_origin.x;
            ev->y = geometry.y + output_origin.y;
            configure_request(wlr_box{ev->x, ev->y, ev->width, ev->height});
        });
        on_set_title.set_callback([&] (void*)
        {
            handle_title_changed(nonull(xw->title));
        });
        on_set_app_id.set_callback([&] (void*)
        {
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
        this->xw = nullptr;
        output_geometry_changed.disconnect();

        on_map.disconnect();
        on_unmap.disconnect();
        on_destroy.disconnect();
        on_configure.disconnect();
        on_set_title.disconnect();
        on_set_app_id.disconnect();

        wf::wlr_view_t::destroy();
    }

    virtual void configure_request(wf::geometry_t configure_geometry)
    {
        /* Wayfire positions views relative to their output, but Xwayland
         * windows have a global positioning. So, we need to make sure that we
         * always transform between output-local coordinates and global
         * coordinates */
        if (get_output())
        {
            auto current_workspace =
                get_output()->workspace->get_current_workspace();
            auto wsize = get_output()->workspace->get_workspace_grid_size();
            auto og    = get_output()->get_layout_geometry();
            configure_geometry.x -= og.x;
            configure_geometry.y -= og.y;

            /* Make sure views don't position themselves outside the desktop
             * area, which is made up of all workspaces for an output */
            wlr_box wsg{
                -current_workspace.x * og.width,
                -current_workspace.y * og.height,
                wsize.width * og.width,
                wsize.height * og.height};
            configure_geometry = wf::clamp(configure_geometry, wsg);
        }

        send_configure(configure_geometry.width, configure_geometry.height);

        if (view_impl->frame)
        {
            configure_geometry =
                view_impl->frame->expand_wm_geometry(configure_geometry);
        }

        set_geometry(configure_geometry);
    }

    virtual void close() override
    {
        if (xw)
        {
            wlr_xwayland_surface_close(xw);
        }

        wf::wlr_view_t::close();
    }

    void send_configure(int width, int height)
    {
        if (!xw)
        {
            return;
        }

        if ((width < 0) || (height < 0))
        {
            /* such a configure request would freeze xwayland.
             * This is most probably a bug somewhere in the compositor. */
            LOGE("Configuring a xwayland surface with width/height <0");

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
        send_configure(last_size_request.width, last_size_request.height);
    }

    void move(int x, int y) override
    {
        wf::wlr_view_t::move(x, y);
        if (!view_impl->in_continuous_move)
        {
            send_configure();
        }
    }

    virtual void set_output(wf::output_t *wo) override
    {
        output_geometry_changed.disconnect();
        wlr_view_t::set_output(wo);

        if (wo)
        {
            wo->connect_signal("output-configuration-changed",
                &output_geometry_changed);
        }

        /* Update the real position */
        if (is_mapped())
        {
            send_configure();
        }
    }
};

xcb_atom_t wayfire_xwayland_view_base::_NET_WM_WINDOW_TYPE_NORMAL;

class wayfire_unmanaged_xwayland_view : public wayfire_xwayland_view_base
{
  public:
    wayfire_unmanaged_xwayland_view(wlr_xwayland_surface *xww);

    int global_x, global_y;

    void commit() override;
    void map(wlr_surface *surface) override;

    ~wayfire_unmanaged_xwayland_view()
    {}
};

class wayfire_xwayland_view : public wayfire_xwayland_view_base
{
    wf::wl_listener_wrapper on_request_move, on_request_resize,
        on_request_maximize, on_request_minimize, on_request_activate,
        on_request_fullscreen, on_set_parent, on_set_decorations, on_set_hints;

  public:
    wayfire_xwayland_view(wlr_xwayland_surface *xww) :
        wayfire_xwayland_view_base(xww)
    {}

    virtual void initialize() override
    {
        LOGE("new xwayland surface ", xw->title,
            " class: ", xw->class_t, " instance: ", xw->instance);
        wayfire_xwayland_view_base::initialize();

        on_request_move.set_callback([&] (void*) { move_request(); });
        on_request_resize.set_callback([&] (void*) { resize_request(); });
        on_request_activate.set_callback([&] (void*)
        {
            if (!this->activated)
            {
                wf::view_self_request_focus_signal data;
                data.view = this;
                wf::get_core().emit_signal("view-self-request-focus", &data);
                this->emit_signal("self-request-focus", &data);
            }
        });

        on_request_maximize.set_callback([&] (void*)
        {
            tile_request((xw->maximized_horz && xw->maximized_vert) ?
                wf::TILED_EDGES_ALL : 0);
        });
        on_request_fullscreen.set_callback([&] (void*)
        {
            fullscreen_request(get_output(), xw->fullscreen);
        });
        on_request_minimize.set_callback([&] (void *data)
        {
            auto ev = (wlr_xwayland_minimize_event*)data;
            minimize_request(ev->minimize);
        });

        on_set_parent.set_callback([&] (void*)
        {
            auto parent = xw->parent ?
                wf::wf_view_from_void(xw->parent->data)->self() : nullptr;
            /* XXX: Do not set parent if parent is unmapped. This happens on
             * some Xwayland clients which have a WM leader and dialogues are
             * then children of this unmapped WM leader... */
            if (!parent || parent->is_mapped())
            {
                set_toplevel_parent(parent);
            }
        });

        on_set_decorations.set_callback([&] (void*)
        {
            update_decorated();
        });

        on_set_hints.set_callback([&] (void*)
        {
            wf::view_hints_changed_signal data;
            data.view = this;
            if (xw->hints_urgency)
            {
                data.demands_attention = true;
            }

            wf::get_core().emit_signal("view-hints-changed", &data);
            this->emit_signal("hints-changed", &data);
        });
        on_set_parent.connect(&xw->events.set_parent);
        on_set_decorations.connect(&xw->events.set_decorations);
        on_set_hints.connect(&xw->events.set_hints);

        on_request_move.connect(&xw->events.request_move);
        on_request_resize.connect(&xw->events.request_resize);
        on_request_activate.connect(&xw->events.request_activate);
        on_request_maximize.connect(&xw->events.request_maximize);
        on_request_minimize.connect(&xw->events.request_minimize);
        on_request_fullscreen.connect(&xw->events.request_fullscreen);

        xw->data = dynamic_cast<wf::view_interface_t*>(this);
        // set initial parent
        on_set_parent.emit(nullptr);
        on_set_decorations.emit(nullptr);
    }

    virtual void destroy() override
    {
        on_set_parent.disconnect();
        on_set_decorations.disconnect();
        on_set_hints.disconnect();
        on_request_move.disconnect();
        on_request_resize.disconnect();
        on_request_activate.disconnect();
        on_request_maximize.disconnect();
        on_request_minimize.disconnect();
        on_request_fullscreen.disconnect();

        wayfire_xwayland_view_base::destroy();
    }

    void emit_view_map() override
    {
        /* Some X clients position themselves on map, and others let the window
         * manager determine this. We try to heuristically guess which of the
         * two cases we're dealing with by checking whether we have recevied
         * a valid ConfigureRequest before mapping */
        bool client_self_positioned = self_positioned;
        emit_view_map_signal(self(), client_self_positioned);
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

            auto view = std::make_unique<wayfire_unmanaged_xwayland_view>(
                xsurface);
            auto view_ptr = view.get();

            wf::get_core().add_view(std::move(view));
            view_ptr->map(xsurface->surface);

            return;
        }

        if (xw->maximized_horz && xw->maximized_vert)
        {
            if ((xw->width > 0) && (xw->height > 0))
            {
                /* Save geometry which the window has put itself in */
                wf::geometry_t save_geometry = {
                    xw->x, xw->y, xw->width, xw->height
                };

                /* Make sure geometry is properly visible on the view output */
                save_geometry = wf::clamp(save_geometry,
                    get_output()->workspace->get_workarea());
                this->view_impl->last_windowed_geometry = save_geometry;
            }

            tile_request(wf::TILED_EDGES_ALL);
        }

        if (xw->fullscreen)
        {
            fullscreen_request(get_output(), true);
        }

        if (!this->tiled_edges && !xw->fullscreen)
        {
            /* Make sure the view is visible on the current workspace
             * on the current output. */
            auto output_geometry     = get_output()->get_layout_geometry();
            wlr_box current_geometry = {xw->x, xw->y, xw->width, xw->height};
            current_geometry = wf::clamp(current_geometry, output_geometry);
            configure_request(current_geometry);
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
        last_size_request.width  = geometry.width;
        last_size_request.height = geometry.height;
    }

    void update_decorated()
    {
        uint32_t csd_flags = WLR_XWAYLAND_SURFACE_DECORATIONS_NO_TITLE |
            WLR_XWAYLAND_SURFACE_DECORATIONS_NO_BORDER;
        this->set_decoration_mode(xw->decorations & csd_flags);
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
        {
            send_configure();
        }
    }

    void resize(int w, int h) override
    {
        if (view_impl->frame)
        {
            view_impl->frame->calculate_resize_size(w, h);
        }

        wf::dimensions_t current_size = {
            get_output_geometry().width,
            get_output_geometry().height
        };
        if (!should_resize_client({w, h}, current_size))
        {
            return;
        }

        this->last_size_request = {w, h};
        send_configure(w, h);
    }

    virtual void request_native_size() override
    {
        if (!is_mapped() || !xw->size_hints)
        {
            return;
        }

        if ((xw->size_hints->base_width > 0) && (xw->size_hints->base_height > 0))
        {
            this->last_size_request = {
                xw->size_hints->base_width,
                xw->size_hints->base_height
            };
            send_configure();
        }
    }

    void set_tiled(uint32_t edges) override
    {
        wf::wlr_view_t::set_tiled(edges);
        wlr_xwayland_surface_set_maximized(xw, !!edges);
    }

    virtual void toplevel_send_app_id() override
    {
        if (!toplevel_handle)
        {
            return;
        }

        /* Xwayland windows have two "app-id"s - the class and the instance.
         * Some apps' icons can be found by looking up the class, for others
         * the instance. So, just like the workaround for gtk-shell, we can
         * send both the instance and the class to clients, so that they can
         * find the appropriate icons. */
        std::string app_id;
        auto default_app_id  = get_app_id();
        auto instance_app_id = nonull(xw->instance);

        std::string app_id_mode =
            wf::option_wrapper_t<std::string>("workarounds/app_id_mode");
        if (app_id_mode == "full")
        {
            app_id = default_app_id + " " + instance_app_id;
        } else
        {
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

    void set_minimized(bool minimized) override
    {
        wf::wlr_view_t::set_minimized(minimized);
        wlr_xwayland_surface_set_minimized(xw, minimized);
    }
};

wayfire_unmanaged_xwayland_view::wayfire_unmanaged_xwayland_view(
    wlr_xwayland_surface *xww) :
    wayfire_xwayland_view_base(xww)
{
    LOGE("new unmanaged xwayland surface ", xw->title, " class: ", xw->class_t,
        " instance: ", xw->instance);

    xw->data = this;
    role     = wf::VIEW_ROLE_UNMANAGED;
}

void wayfire_unmanaged_xwayland_view::commit()
{
    /* Xwayland O-R views manage their position on their own. So we need to
     * update their position on each commit, if the position changed. */
    if ((global_x != xw->x) || (global_y != xw->y))
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
        auto gc = wf::get_core().get_cursor_position();
        wo = wf::get_core().output_layout->get_output_at(gc.x, gc.y);
    }

    if (!wo)
    {
        wo = wf::get_core().get_active_output();
    }

    assert(wo);

    auto real_output_geometry = wo->get_layout_geometry();

    global_x = xw->x;
    global_y = xw->y;
    wf::wlr_view_t::move(xw->x - real_output_geometry.x,
        xw->y - real_output_geometry.y);

    if (wo != get_output())
    {
        if (get_output())
        {
            get_output()->workspace->remove_view(self());
        }

        set_output(wo);
    }

    damage();

    /* We update the keyboard focus before emitting the map event, so that
     * plugins can detect that this view can have keyboard focus */
    view_impl->keyboard_focus_enabled = wlr_xwayland_or_surface_wants_focus(xw);

    get_output()->workspace->add_view(self(), wf::LAYER_UNMANAGED);
    wf::wlr_view_t::map(surface);

    if (wlr_xwayland_or_surface_wants_focus(xw))
    {
        get_output()->focus_view(self());
    }
}

static wlr_xwayland *xwayland_handle = nullptr;
#endif

void wf::init_xwayland()
{
#if WF_HAS_XWAYLAND
    static wf::wl_listener_wrapper on_created;
    static wf::wl_listener_wrapper on_ready;

    static signal_connection_t on_shutdown{[&] (void*)
        {
            wlr_xwayland_destroy(xwayland_handle);
        }
    };

    on_created.set_callback([] (void *data)
    {
        auto xsurf = (wlr_xwayland_surface*)data;
        if (xsurf->override_redirect)
        {
            wf::get_core().add_view(
                std::make_unique<wayfire_unmanaged_xwayland_view>(xsurf));
        } else
        {
            wf::get_core().add_view(
                std::make_unique<wayfire_xwayland_view>(xsurf));
        }
    });

    on_ready.set_callback([] (void *data)
    {
        if (!wayfire_xwayland_view_base::load_atoms())
        {
            LOGE("Failed to load Xwayland atoms.");
        } else
        {
            LOGD("Successfully loaded Xwayland atoms.");
        }
    });

    xwayland_handle = wlr_xwayland_create(wf::get_core().display,
        wf::get_core_impl().compositor, false);

    if (xwayland_handle)
    {
        on_created.connect(&xwayland_handle->events.new_surface);
        on_ready.connect(&xwayland_handle->events.ready);
        wf::get_core().connect_signal("shutdown", &on_shutdown);
    }

#endif
}

void wf::xwayland_set_seat(wlr_seat *seat)
{
#if WF_HAS_XWAYLAND
    if (xwayland_handle)
    {
        wlr_xwayland_set_seat(xwayland_handle,
            wf::get_core().get_current_seat());
    }

#endif
}

void wf::xwayland_set_cursor(wlr_xcursor_image *image)
{
#if WF_HAS_XWAYLAND
    if (!xwayland_handle)
    {
        return;
    }

    wlr_xwayland_set_cursor(xwayland_handle, image->buffer,
        image->width * 4, image->width, image->height,
        image->hotspot_x, image->hotspot_y);
#endif
}

std::string wf::xwayland_get_display()
{
#if WF_HAS_XWAYLAND

    return xwayland_handle ? nonull(xwayland_handle->display_name) : "";
#else

    return "";
#endif
}
