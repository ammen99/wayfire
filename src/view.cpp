#include "debug.hpp"
#include "core.hpp"
#include "opengl.hpp"
#include "output.hpp"
#include "view.hpp"
#include "workspace-manager.hpp"
#include "render-manager.hpp"
#include "desktop-api.hpp"

#include <algorithm>
#include <glm/glm.hpp>
#include "signal-definitions.hpp"

extern "C"
{
#define static
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#undef static
}

/* misc definitions */

glm::mat4 wayfire_view_transform::global_rotation;
glm::mat4 wayfire_view_transform::global_scale;
glm::mat4 wayfire_view_transform::global_translate;
glm::mat4 wayfire_view_transform::global_view_projection;

glm::mat4 wayfire_view_transform::calculate_total_transform()
{
    return global_view_projection * (global_translate * translation) *
           (global_rotation * rotation) * (global_scale * scale);
}

bool operator == (const wf_geometry& a, const wf_geometry& b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool operator != (const wf_geometry& a, const wf_geometry& b)
{
    return !(a == b);
}

bool point_inside(wf_point point, wf_geometry rect)
{
    if(point.x < rect.x || point.y < rect.y)
        return false;

    if(point.x > rect.x + rect.width)
        return false;

    if(point.y > rect.y + rect.height)
        return false;

    return true;
}

bool rect_intersect(wf_geometry screen, wf_geometry win)
{
    if (win.x + (int32_t)win.width <= screen.x ||
        win.y + (int32_t)win.height <= screen.y)
        return false;

    if (screen.x + (int32_t)screen.width <= win.x ||
        screen.y + (int32_t)screen.height <= win.y)
        return false;
    return true;
}

/*

void desktop_surface_removed(weston_desktop_surface *surface, void *user_data)
{
    view->destroyed = true;

    if (view->output)
    {
        auto sig_data = destroy_view_signal{view};
        view->output->emit_signal("destroy-view", &sig_data);

        if (view->parent)
        {
            auto it = std::find(view->parent->children.begin(), view->parent->children.end(), view);
            assert(it != view->parent->children.end());
            view->parent->children.erase(it);
        }
    }

    core->erase_view(view, view->keep_count <= 0);
}



void desktop_surface_set_parent(weston_desktop_surface *ds,
                                weston_desktop_surface *parent_ds,
                                void *data)
{
    auto view = core->find_view(ds);
    auto parent = core->find_view(parent_ds);

    if (!view || !parent)
        return;

    view->parent = parent;
    parent->children.push_back(view);

    view_set_parent_signal sdata; sdata.view = view;
    view->output->emit_signal("view-set-parent", &sdata);
} */

void committed_cb(wl_listener*, void *data)
{
    auto view = core->find_view((wlr_surface*) data);
    view->commit();
}

// TODO: do better
void destroyed_cb(wl_listener*, void *data)
{
    auto view = core->find_view((wlr_surface*) data);

    view->output->detach_view(view);
    core->erase_view(view);
}

wayfire_view_t::wayfire_view_t(wlr_surface *surf)
{
    output  = core->get_active_output();
    output->render->schedule_redraw();
    surface = surf;

    geometry.x = geometry.y = 0;
    geometry.width = surface->current->width;
    geometry.height = surface->current->height;

    transform.color = glm::vec4(1, 1, 1, 1);

    committed.notify = committed_cb;
    wl_signal_add(&surface->events.commit, &committed);

    destroy.notify = destroyed_cb;
    wl_signal_add(&surface->events.destroy, &destroy);
}

wayfire_view_t::~wayfire_view_t()
{
    for (auto& kv : custom_data)
        delete kv.second;
}

wayfire_view wayfire_view_t::self()
{
    return core->find_view(surface);
}

// TODO: implement is_visible
bool wayfire_view_t::is_visible()
{
    return true;
}

void wayfire_view_t::update_size()
{
    geometry.width = surface->current ? surface->current->width  : 0;
    geometry.height = surface->current? surface->current->height : 0;
}

void wayfire_view_t::set_moving(bool moving)
{
    in_continuous_move += moving ? 1 : -1;
}

void wayfire_view_t::set_resizing(bool resizing)
{
    in_continuous_resize += resizing ? 1 : -1;
}

void wayfire_view_t::move(int x, int y, bool send_signal)
{
    view_geometry_changed_signal data;
    data.view = core->find_view(surface);
    data.old_geometry = geometry;

    damage();
    geometry.x = x;
    geometry.y = y;
    damage();

    if (send_signal)
        output->emit_signal("view-geometry-changed", &data);
}

void wayfire_view_t::resize(int w, int h, bool send_signal)
{
    view_geometry_changed_signal data;
    data.view = core->find_view(surface);
    data.old_geometry = geometry;

    damage();
    geometry.width = w;
    geometry.height = h;
    damage();

    if (send_signal)
        output->emit_signal("view-geometry-changed", &data);
}

wlr_surface* wayfire_view_t::map_input_coordinates(int cx, int cy, int& sx, int& sy)
{
    wlr_surface *ret = NULL;

    for_each_surface(
        [&] (wlr_surface *surface, int x, int y)
        {
            if (ret) return;

            sx = cx - x;
            sy = cy - y;

            if (wlr_surface_point_accepts_input(surface, sx, sy))
                ret = surface;
        });

    return ret;
}

static void for_each_subsurface(wlr_surface *root,
                                int root_x, int root_y,
                                wf_surface_iterator_callback call,
                                bool reverse = false)
{
    if (reverse)
        call(root, root_x, root_y);

    wlr_subsurface *sub;
    wl_list_for_each(sub, &root->subsurface_list, parent_link)
    {
        int child_x = root_x + sub->surface->current->subsurface_position.x;
        int child_y = root_y + sub->surface->current->subsurface_position.y;
        for_each_subsurface(sub->surface, child_x, child_y, call);
    }

    if (!reverse)
        call(root, root_x, root_y);
}

void wayfire_view_t::for_each_surface(wf_surface_iterator_callback call, bool reverse)
{
    auto og = get_output_geometry();
    for_each_subsurface(surface, og.x, og.y, call, reverse);
}

void wayfire_view_t::set_geometry(wf_geometry g)
{
    move(g.x, g.y, false);
    resize(g.width, g.height);
}

void wayfire_view_t::set_maximized(bool maxim)
{
    maximized = maxim;
}

void wayfire_view_t::set_fullscreen(bool full)
{
    fullscreen = full;
}

void wayfire_view_t::set_parent(wayfire_view parent)
{
    if (this->parent)
    {
        auto it = std::remove(this->parent->children.begin(), this->parent->children.end(), self());
        this->parent->children.erase(it);
    }

    this->parent = parent;
    if (parent)
    {
        auto it = std::find(parent->children.begin(), parent->children.end(), self());
        if (it == parent->children.end())
            parent->children.push_back(self());
    }
}

void wayfire_view_t::map()
{
    auto workarea = output->workspace->get_workarea();
    geometry.x += workarea.x;
    geometry.y += workarea.y;

    log_info("mapping a view %d", is_special);
    update_size();
    if (is_mapped)
    {
        log_error ("request to map %p twice!", surface);
        return;
    }

    is_mapped = true;

    /* TODO: consider not emitting a create-view for special surfaces */
    create_view_signal data;
    data.view = self();
    output->emit_signal("create-view", &data);


    if (!is_special)
    {
        output->focus_view(self());

        /* TODO: check mods
           auto seat = core->get_current_seat();
           auto kbd = seat ? weston_seat_get_keyboard(seat) : NULL;

           if (kbd)
           {
           we send zero depressed modifiers, because some modifiers are
         * stuck when opening a window(for example if the app was opened while some plugin
         * was working or similar)
         weston_keyboard_send_modifiers(kbd, wl_display_next_serial(core->ec->wl_display),
         0, kbd->modifiers.mods_latched,
         kbd->modifiers.mods_locked, kbd->modifiers.group);
         } */
    }

    return;
}

void wayfire_view_t::commit()
{
    update_size();

    /* TODO: do this check in constructors
    auto full  = weston_desktop_surface_get_fullscreen(desktop_surface),
         maxim = weston_desktop_surface_get_maximized(desktop_surface);

    if (full != fullscreen)
    {
        view_fullscreen_signal data;
        data.view = core->find_view(handle);
        data.state = full;
        output->emit_signal("view-fullscreen-request", &data);

        set_fullscreen(full);
    } else if (maxim != maximized)
    {
        view_maximized_signal data;
        data.view = core->find_view(handle);
        data.state = maximized;

    } */
}

void wayfire_view_t::damage()
{
    output->render->damage(get_output_geometry());
}

#define toplevel_op_check \
    if(!is_toplevel())\
    { \
        log_error("view.cpp(%d): attempting to %s a non-toplevel view", __LINE__, __func__); \
        return; \
    }

static inline void handle_move_request(wayfire_view view)
{
    move_request_signal data;
    data.view = view;
    view->output->emit_signal("move-request", &data);
}

static inline void handle_resize_request(wayfire_view view)
{
    resize_request_signal data;
    data.view = view;
    view->output->emit_signal("resize-request", &data);
}

static inline void handle_maximize_request(wayfire_view view, bool state)
{
    if (!view || view->maximized == state)
        return;

    view_maximized_signal data;
    data.view = view;
    data.state = state;

    if (view->is_mapped)
    {
        view->output->emit_signal("view-maximized-request", &data);
    } else if (state)
    {
        view->set_geometry(view->output->workspace->get_workarea());
        view->output->emit_signal("view-maximized", &data);
    }
}

void handle_fullscreen_request(wayfire_view view, wayfire_output *output, bool state)
{
    if (!view || view->fullscreen == state)
        return;

    auto wo = (output ? output : view->output);
    wo = (wo ? wo : core->get_active_output());
    assert(wo);

    if (view->output != wo)
    {
        auto pg = view->output->get_full_geometry();
        auto ng = wo->get_full_geometry();

        core->move_view_to_output(view, wo);
        view->move(view->geometry.x + ng.x - pg.x, view->geometry.y + ng.y - pg.y);
    }

    view_fullscreen_signal data;
    data.view = view;
    data.state = state;

    if (view->is_mapped) {
        wo->emit_signal("view-fullscreen-request", &data);
    } else if (state) {
        view->set_geometry(view->output->get_full_geometry());
        view->output->emit_signal("view-fullscreen", &data);
    }

    view->set_fullscreen(state);
}

/* xdg_shell_v6 implementation */
/* TODO: unmap, popups */

static void handle_v6_map(wl_listener*, void *data)
{
    auto surface = static_cast<wlr_xdg_surface_v6*> (data);
    auto view = core->find_view(surface->surface);

    assert(view);

    view->map();
}

static void handle_v6_request_move(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_xdg_toplevel_v6_move_event*> (data);
    auto view = core->find_view(ev->surface->surface);
    handle_move_request(view);
}

static void handle_v6_request_resize(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_xdg_toplevel_v6_resize_event*> (data);
    auto view = core->find_view(ev->surface->surface);
    handle_resize_request(view);
}

static void handle_v6_request_maximized(wl_listener*, void *data)
{
    auto surf = static_cast<wlr_xdg_surface_v6*> (data);
    auto view = core->find_view(surf->surface);
    handle_maximize_request(view, surf->toplevel->client_pending.maximized);
}

static void handle_v6_request_fullscreen(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_xdg_toplevel_v6_set_fullscreen_event*> (data);
    auto view = core->find_view(ev->surface->surface);
    auto wo = core->get_output(ev->output);
    handle_fullscreen_request(view, wo, ev->fullscreen);
}

void handle_new_popup(wl_listener*, void*);
void handle_popup_map(wl_listener*, void*);
void handle_popup_unmap(wl_listener*, void*);
void handle_popup_destroy(wl_listener*, void*);

/* xdg_popup_v6 implementation
 * Currently we use a "hack": we treat the toplevel as a special popup,
 * so that we can use the same functions for adding a new popup, tracking them, etc. */

/* TODO: Figure out a way to animate this, possibly implement wayfire_renderable_t */
class wayfire_xdg6_popup
{
    protected:
        wl_listener new_popup, destroy_popup,
                    m_popup_map, m_popup_unmap;

                wlr_xdg_surface_v6 *base_surface;

        wayfire_xdg6_popup(wayfire_output*& output, wlr_xdg_surface_v6 *base)
            :base_surface(base), m_popup_output(output)
        {
            core->api->xdg_v6_popups[base_surface] = this;

            new_popup.notify     = handle_new_popup;
            destroy_popup.notify = handle_popup_destroy;
            m_popup_map.notify   = handle_popup_map;
            m_popup_unmap.notify = handle_popup_unmap;

            wl_signal_add(&base_surface->events.new_popup, &new_popup);

            if (base_surface->role == WLR_XDG_SURFACE_V6_ROLE_POPUP)
            {
                wl_signal_add(&base_surface->events.destroy,   &destroy_popup);
                wl_signal_add(&base_surface->events.map,   &m_popup_map);
                wl_signal_add(&base_surface->events.unmap, &m_popup_unmap);
            }
        }

    public:
        bool m_popup_mapped = false;

        wayfire_xdg6_popup *parent_popup = NULL;
        wlr_xdg_popup_v6 *m_popup = NULL;
        wayfire_output*& m_popup_output;

        std::vector<wayfire_xdg6_popup*> m_children;

        wayfire_xdg6_popup(wayfire_output*& output, wlr_xdg_popup_v6 *pop)
            : wayfire_xdg6_popup(output, pop->base)
        {
            m_popup = pop;
        }

        virtual ~wayfire_xdg6_popup()
        {
            core->api->xdg_v6_popups.erase(base_surface);
            for (auto c : m_children)
                delete c;
        }
};

void handle_new_popup(wl_listener*, void *data)
{
    auto popup = static_cast<wlr_xdg_popup_v6*> (data);
    auto it = core->api->xdg_v6_popups.find(popup->parent);
    if (it == core->api->xdg_v6_popups.end())
    {
        log_error("attempting to create a popup with unknown parent");
        return;
    }

    auto parent = it->second;
    auto pop = new wayfire_xdg6_popup(parent->m_popup_output, popup);

    pop->parent_popup = parent;
    parent->m_children.push_back(pop);
}

/* TODO: damage from popups, recursive till top */
void handle_popup_map(wl_listener*, void *data)
{
    auto popup = static_cast<wlr_xdg_surface_v6*> (data);
    auto it = core->api->xdg_v6_popups.find(popup);
    if (it == core->api->xdg_v6_popups.end())
    {
        log_error("attempting to map an unknown popup");
        return;
    }

    it->second->m_popup_mapped = true;
}

void handle_popup_unmap(wl_listener*, void *data)
{
    auto popup = static_cast<wlr_xdg_surface_v6*> (data);
    auto it = core->api->xdg_v6_popups.find(popup);
    if (it == core->api->xdg_v6_popups.end())
    {
        log_error("attempting to unmap an unknown popup");
        return;
    }

    it->second->m_popup_mapped = false;
}

void handle_popup_destroy(wl_listener*, void *data)
{
    auto popup = static_cast<wlr_xdg_surface_v6*> (data);
    auto it = core->api->xdg_v6_popups.find(popup);
    if (it == core->api->xdg_v6_popups.end())
    {
        log_error("attempting to destroy an unknown popup");
        return;
    }

    auto wf_popup = it->second;
    wf_popup->m_popup_mapped = false;

    auto parent = it->second->parent_popup;
    if (parent)
    {
        auto pit = parent->m_children.begin();
        while (pit != parent->m_children.end())
        {
            if (*pit == wf_popup)
                pit = parent->m_children.erase(pit);
            else
                ++pit;
        }
    } else
    {
        log_error("attempting to destroy a popup without a parent!");
    }

    delete wf_popup;
}

/* the top call for xdg6_popup_for_each_surface is with a toplevel surface
 * which has m_popup = NULL, we must make sure to handle this case */
static void xdg6_popup_for_each_surface(wayfire_xdg6_popup *popup,
                                        int popup_x, int popup_y,
                                        wf_surface_iterator_callback call,
                                        bool reverse)
{
    if (popup->m_popup && reverse)
    {
        for_each_subsurface(popup->m_popup->base->surface,
                            popup_x, popup_y, call, reverse);
    }

    for (auto p : popup->m_children)
    {
        if (p->m_popup_mapped)
        {
            double px, py;
            wlr_xdg_surface_v6_popup_get_position(p->m_popup->base, &px, &py);
            xdg6_popup_for_each_surface(p, popup_x + px, popup_y + py, call, reverse);
        }
    }

    if (popup->m_popup && !reverse)
    {
        for_each_subsurface(popup->m_popup->base->surface,
                            popup_x, popup_y, call, reverse);
    }
}


class wayfire_xdg6_view : public wayfire_view_t, wayfire_xdg6_popup
{
    wlr_xdg_surface_v6 *v6_surface;
    wl_listener map,
                request_move, request_resize,
                request_maximize, request_fullscreen;


    public:
    wayfire_xdg6_view(wlr_xdg_surface_v6 *s)
        : wayfire_view_t (s->surface), wayfire_xdg6_popup(output, s), v6_surface(s)
    {
        log_info ("new xdg_shell_v6 surface: %s app-id: %s",
                  nonull(v6_surface->toplevel->title),
                  nonull(v6_surface->toplevel->app_id));

        map.notify                = handle_v6_map;
        request_move.notify       = handle_v6_request_move;
        request_resize.notify     = handle_v6_request_resize;
        request_maximize.notify   = handle_v6_request_maximized;
        request_fullscreen.notify = handle_v6_request_fullscreen;

        wlr_xdg_surface_v6_ping(s);

        wl_signal_add(&v6_surface->events.map, &map);
        wl_signal_add(&v6_surface->toplevel->events.request_move,       &request_move);
        wl_signal_add(&v6_surface->toplevel->events.request_resize,     &request_resize);
        wl_signal_add(&v6_surface->toplevel->events.request_maximize,   &request_maximize);
        wl_signal_add(&v6_surface->toplevel->events.request_fullscreen, &request_fullscreen);
    }

    bool is_toplevel()
    {
        return v6_surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL;
    }

    wf_geometry get_output_geometry()
    {
        return {
            geometry.x - v6_surface->geometry.x,
            geometry.y - v6_surface->geometry.y,
            surface->current ? surface->current->width : 0,
            surface->current ? surface->current->height : 0
        };
    }

    void update_size()
    {
        if (v6_surface->geometry.width > 0 && v6_surface->geometry.height > 0)
        {
            geometry.width = v6_surface->geometry.width;
            geometry.height = v6_surface->geometry.height;
        } else
        {
            wayfire_view_t::update_size();
        }
    }

    void activate(bool act)
    {
        toplevel_op_check;
        wayfire_view_t::activate(act);
        wlr_xdg_toplevel_v6_set_activated(v6_surface, act);
    }

    void set_maximized(bool max)
    {
        toplevel_op_check;
        wayfire_view_t::set_maximized(max);
        wlr_xdg_toplevel_v6_set_maximized(v6_surface, max);
    }

    void set_fullscreen(bool full)
    {
        toplevel_op_check;
        wayfire_view_t::set_fullscreen(full);
        wlr_xdg_toplevel_v6_set_fullscreen(v6_surface, full);
    }

    void move(int w, int h, bool send)
    {
        wayfire_view_t::move(w, h, send);
    }

    void resize(int w, int h, bool send)
    {
        toplevel_op_check;

        wayfire_view_t::resize(w, h, send);
        wlr_xdg_toplevel_v6_set_size(v6_surface, w, h);
    }

    void for_each_surface(wf_surface_iterator_callback call, bool reverse)
    {
        auto og = get_output_geometry();

        if (reverse)
        {
            for_each_subsurface(surface, og.x, og.y, call, reverse);
            xdg6_popup_for_each_surface(this, og.x, og.y, call, reverse);
        } else
        {
            xdg6_popup_for_each_surface(this, og.x, og.y, call, reverse);
            for_each_subsurface(surface, og.x, og.y, call);
        }
    }

    ~wayfire_xdg6_view()
    {
    }
};



void notify_v6_created(wl_listener*, void *data)
{
    auto surf = static_cast<wlr_xdg_surface_v6*> (data);

    if (surf->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL)
        core->add_view(std::make_shared<wayfire_xdg6_view> ((wlr_xdg_surface_v6*)data));
}

/* end of xdg_shell_v6 implementation */

/* xwayland implementation */
static void handle_xwayland_request_move(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_xwayland_move_event*> (data);
    auto view = core->find_view(ev->surface->surface);
    handle_move_request(view);
}

static void handle_xwayland_request_resize(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_xwayland_resize_event*> (data);
    auto view = core->find_view(ev->surface->surface);
    handle_resize_request(view);
}

static void handle_xwayland_request_configure(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_xwayland_surface_configure_event*> (data);
    auto view = core->find_view(ev->surface->surface);
    view->set_geometry({ev->x, ev->y, ev->width, ev->height});
}

static void handle_xwayland_request_maximize(wl_listener*, void *data)
{
    auto surf = static_cast<wlr_xwayland_surface*> (data);
    auto view = core->find_view(surf->surface);
    handle_maximize_request(view, surf->maximized_horz && surf->maximized_vert);
}

static void handle_xwayland_request_fullscreen(wl_listener*, void *data)
{
    auto surf = static_cast<wlr_xwayland_surface*> (data);
    auto view = core->find_view(surf->surface);
    handle_fullscreen_request(view, view->output, surf->fullscreen);
}

class wayfire_xwayland_view : public wayfire_view_t
{
    wlr_xwayland_surface *xw;
    wl_listener configure,
                request_move, request_resize,
                request_maximize, request_fullscreen;

    public:
    wayfire_xwayland_view(wlr_xwayland_surface *xww)
        : wayfire_view_t(xww->surface), xw(xww)
    {
        log_info("new xwayland surface %s class: %s instance: %s",
                 nonull(xw->title), nonull(xw->class_t), nonull(xw->instance));
        map();

        configure.notify          = handle_xwayland_request_configure;
        request_move.notify       = handle_xwayland_request_move;
        request_resize.notify     = handle_xwayland_request_resize;
        request_maximize.notify   = handle_xwayland_request_maximize;
        request_fullscreen.notify = handle_xwayland_request_fullscreen;

        wl_signal_add(&xw->events.request_move,       &request_move);
        wl_signal_add(&xw->events.request_resize,     &request_resize);
        wl_signal_add(&xw->events.request_maximize,   &request_maximize);
        wl_signal_add(&xw->events.request_fullscreen, &request_fullscreen);
        wl_signal_add(&xw->events.request_configure,  &configure);
    }

    void activate(bool active)
    {
        wayfire_view_t::activate(active);
        wlr_xwayland_surface_activate(xw, active);
    }

    void move(int x, int y, bool s)
    {
        geometry.x = x;
        geometry.y = y;
        set_geometry(geometry);
    }

    void resize(int w, int h, bool s)
    {
        wayfire_view_t::resize(w, h, s);
        wlr_xwayland_surface_configure(xw, geometry.x, geometry.y,
                                       geometry.width, geometry.height);
    }

    void set_geometry(wf_geometry g)
    {
        this->geometry = g;
        resize(geometry.width, geometry.height, true);
    }

    void close()
    {
        wlr_xwayland_surface_close(xw);
    }

    void set_maximized(bool maxim)
    {
        wayfire_view_t::set_maximized(maxim);
        wlr_xwayland_surface_set_maximized(xw, maxim);

    }

    void set_fullscreen(bool full)
    {
        wayfire_view_t::set_fullscreen(full);
        wlr_xwayland_surface_set_fullscreen(xw, full);
    }
};

void notify_xwayland_created(wl_listener *, void *data)
{
    core->add_view(std::make_shared<wayfire_xwayland_view> ((wlr_xwayland_surface*) data));
}
/* end of xwayland implementation */

void init_desktop_apis()
{
    core->api = new desktop_apis_t;

    core->api->v6_created.notify = notify_v6_created;
    core->api->v6 = wlr_xdg_shell_v6_create(core->display);
    wl_signal_add(&core->api->v6->events.new_surface, &core->api->v6_created);

    core->api->xwayland_created.notify = notify_xwayland_created;
    core->api->xwayland = wlr_xwayland_create(core->display, core->compositor);
    wl_signal_add(&core->api->xwayland->events.new_surface, &core->api->xwayland_created);
}

static void render_surface(wlr_surface *surface, pixman_region32_t *damage,
        int x, int y, glm::mat4, glm::vec4, uint32_t bits);

/* TODO: use bits */
void wayfire_view_t::simple_render(uint32_t bits, pixman_region32_t *damage)
{
    pixman_region32_t our_damage;
    bool free_damage = false;
    auto og = output->get_full_geometry();

    if (damage == nullptr)
    {
        pixman_region32_init_rect(&our_damage, og.x, og.y, og.width, og.height);
        damage = &our_damage;
        free_damage = true;
    }

    pixman_region32_translate(damage, -og.x, -og.y);

    render_surface(surface, damage,
            geometry.x - og.x, geometry.y - og.y,
            transform.calculate_total_transform(), transform.color, bits);

    pixman_region32_translate(damage, og.x, og.y);

    if (free_damage)
        pixman_region32_fini(&our_damage);
}

void wayfire_view_t::render(uint32_t bits, pixman_region32_t *damage)
{

    //log_info("start it");
    for_each_surface([=] (wlr_surface *surface, int x, int y)
                     {
     //                    log_info("got surface %d %d %p", x, y, surface->texture);
                         if (!surface->texture)
                             return;

                         auto rr = wlr_backend_get_renderer(core->backend);

                         float matrix[9];
                         wlr_box g;

                         g.x = x;
                         g.y = y;
                         g.width = surface->current->width;
                         g.height = surface->current->height;

      //                   log_info("%d %d %d %d", g.x, g.y, g.width, g.height);

                         wlr_matrix_project_box(matrix, &g,
                                                surface->current->transform, 0, output->handle->transform_matrix);
                         wlr_render_texture_with_matrix(rr, surface->texture, matrix, 1);

                         struct timespec now;
                         clock_gettime(CLOCK_MONOTONIC, &now);
                         wlr_surface_send_frame_done(surface, &now);
                     }, true);

    return;

    simple_render(bits, damage);


    std::vector<effect_hook_t*> hooks_to_run;
    for (auto hook : effects)
        hooks_to_run.push_back(hook);

    for (auto hook : hooks_to_run)
        (*hook)();
}

static inline void render_surface_box(GLuint tex[3], int n_tex, const pixman_box32_t& surface_box,
        const pixman_box32_t& subbox, glm::mat4 transform,
        glm::vec4 color, uint32_t bits)
{
    OpenGL::texture_geometry texg = {
        1.0f * (subbox.x1 - surface_box.x1) / (surface_box.x2 - surface_box.x1),
        1.0f * (subbox.y1 - surface_box.y1) / (surface_box.y2 - surface_box.y1),
        1.0f * (subbox.x2 - surface_box.x1) / (surface_box.x2 - surface_box.x1),
        1.0f * (subbox.y2 - surface_box.y1) / (surface_box.y2 - surface_box.y1),
    };

    wf_geometry geometry =
    {
        subbox.x1, subbox.y1,
        subbox.x2 - subbox.x1, subbox.y2 - subbox.y1
    };

    OpenGL::use_default_program();
    for (int i = 0; i < n_tex; i++) {
        OpenGL::render_transformed_texture(tex[i], geometry, texg, transform,
                                           color, bits);
    }
}

static void render_surface(wlr_surface *surface, pixman_region32_t *damage,
        int x, int y, glm::mat4 transform, glm::vec4 color, uint32_t bits)

{
    /* TODO: update damage */
    /*
    pixman_region32_t damaged_region;
    pixman_region32_init_rect(&damaged_region, x, y,
            surface->width, surface->height);
    pixman_region32_intersect(&damaged_region, &damaged_region, damage);

    pixman_box32_t surface_box;
    surface_box.x1 = x; surface_box.y1 = y;
    surface_box.x2 = x + surface->width; surface_box.y2 = y + surface->height;

    int n = 0;
    pixman_box32_t *boxes = pixman_region32_rectangles(&damaged_region, &n);

    int n_tex;
    GLuint *tex = (GLuint*)render_manager::renderer_api->surface_get_textures(surface, &n_tex);

    for (int i = 0; i < n; i++) {
        render_surface_box(tex, n_tex, surface_box, boxes[i],
                transform, color, bits | TEXTURE_USE_TEX_GEOMETRY);
    }
    pixman_region32_fini(&damaged_region);

    weston_subsurface *sub;
    if (!wl_list_empty(&surface->subsurface_list)) {
        wl_list_for_each(sub, &surface->subsurface_list, parent_link) {
            if (sub && sub->surface != surface) {
                render_surface(sub->surface, damage,
                        sub->position.x + x, sub->position.y + y,
                        transform, color, bits);
            }
        }
    } */
}
