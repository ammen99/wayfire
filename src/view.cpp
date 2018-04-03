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

/* wayfire_surface_t implementation */
void surface_committed_cb(wl_listener*, void *data)
{
    auto surface = core->api->desktop_surfaces[(wlr_surface*) data];
    assert(surface);

    surface->commit();
}

// TODO: do better
void surface_destroyed_cb(wl_listener*, void *data)
{
    auto surface = core->api->desktop_surfaces[(wlr_surface*) data];
    assert(surface);

    auto view = core->find_view(surface->surface);
    if (view)
    {
        core->erase_view(view);
        return;
    }

    /* Probably do something else? */
    delete surface;
}

void subsurface_created_cb(wl_listener*, void *data)
{
    auto sub = static_cast<wlr_subsurface*> (data);

    auto parent = core->api->desktop_surfaces[sub->parent];
    if (!parent)
    {
        log_error ("subsurface_created with invalid parent!");
        return;
    }

    new wayfire_surface_t(sub->surface, parent);
}

wayfire_surface_t::wayfire_surface_t(wlr_surface *surface, wayfire_surface_t* parent)
{
    this->surface = surface;
    this->parent_surface = parent;

    /* map by default if this is a subsurface, only toplevels/popups have map/unmap events */
    if (surface->subsurface)
        is_mapped = true;

    if (parent)
    {
        set_output(parent->output);
        parent->surface_children.push_back(this);
    }

    new_sub.notify   = subsurface_created_cb;
    committed.notify = surface_committed_cb;
    destroy.notify   = surface_destroyed_cb;

    wl_signal_add(&surface->events.new_subsurface, &new_sub);
    wl_signal_add(&surface->events.commit,         &committed);
    wl_signal_add(&surface->events.destroy,        &destroy);

    core->api->desktop_surfaces[surface] = this;
}

wayfire_surface_t::~wayfire_surface_t()
{
    core->api->desktop_surfaces.erase(surface);

    if (parent_surface)
    {
        auto it = parent_surface->surface_children.begin();
        while(it != parent_surface->surface_children.end())
        {
            if (*it == this)
                it = parent_surface->surface_children.erase(it);
            else
                ++it;
        }
    }

    for (auto c : surface_children)
        delete c;
}

void wayfire_surface_t::get_child_position(int &x, int &y)
{
    x = surface->current->subsurface_position.x;
    y = surface->current->subsurface_position.y;
}

wf_point wayfire_surface_t::get_output_position()
{
    /* if we reach a toplevel, it should override get_output_position */
    assert(parent_surface != NULL);

    auto pos = parent_surface->get_output_position();

    int dx, dy;
    get_child_position(dx, dy);
    pos.x += dx; pos.y += dy;

    return pos;
}

wf_geometry wayfire_surface_t::get_output_geometry()
{
    auto pos = get_output_position();

    return {
        pos.x, pos.y,
        surface->current ? surface->current->width : 0,
        surface->current ? surface->current->height : 0
    };
}

void wayfire_surface_t::commit()
{
    pixman_region32_t damage;
    pixman_region32_init(&damage);
    pixman_region32_copy(&damage, &surface->current->buffer_damage);

    auto pos = get_output_position();
    pixman_region32_translate(&damage, pos.x, pos.y);

    /* TODO: transform damage */
    output->render->damage(&damage);
}

void wayfire_surface_t::set_output(wayfire_output *out)
{
    output = out;
    for (auto c : surface_children)
        c->set_output(out);
}

void wayfire_surface_t::for_each_surface_recursive(wf_surface_iterator_callback call,
                                                   int x, int y, bool reverse)
{
    if (reverse)
        call(this, x, y);

    int dx, dy;
    for (auto c : surface_children)
    {
        c->get_child_position(dx, dy);
        c->for_each_surface_recursive(call, x + dx, y + dy, reverse);
    }

    if (!reverse)
        call(this, x, y);
}

void wayfire_surface_t::for_each_surface(wf_surface_iterator_callback call, bool reverse)
{
    auto pos = get_output_position();
    for_each_surface_recursive(call, pos.x, pos.y, reverse);
}

static wlr_box get_scissor_box(wayfire_output *output, wlr_box *box)
{
    int ow, oh;
    wlr_output_transformed_resolution(output->handle, &ow, &oh);

    wlr_box result;
    memcpy(&result, box, sizeof(result));

    // Scissor is in renderer coordinates, ie. upside down
    enum wl_output_transform transform = wlr_output_transform_compose(
        wlr_output_transform_invert(output->handle->transform),
        WL_OUTPUT_TRANSFORM_FLIPPED_180);

    wlr_box_transform(box, transform, ow, oh, &result);
    return result;
}

void wayfire_surface_t::render(int x, int y, wlr_box *damage)
{
    if (!surface->texture)
        return;

    wlr_box geometry;

    geometry.x = x;
    geometry.y = y;
    geometry.width = surface->current->width;
    geometry.height = surface->current->height;

    if (!damage) damage = &geometry;

    auto rr = core->renderer;
    float matrix[9];
    wlr_matrix_project_box(matrix, &geometry,
                           surface->current->transform,
                           0, output->handle->transform_matrix);

    auto box = get_scissor_box(output, damage);
    wlr_renderer_scissor(rr, &box);

    wlr_render_texture_with_matrix(rr, surface->texture, matrix, 1);

}

void wayfire_surface_t::render_pixman(int x, int y, pixman_region32_t *damage)
{
    int n_rect;
    auto rects = pixman_region32_rectangles(damage, &n_rect);

    for (int i = 0; i < n_rect; i++)
    {
        wlr_box d;
        d.x = rects[i].x1;
        d.y = rects[i].y1;
        d.width = rects[i].x2 - rects[i].x1;
        d.height = rects[i].y2 - rects[i].y1;

        render(x, y, &d);
    }
}

/* wayfire_view_t implementation */
wayfire_view_t::wayfire_view_t(wlr_surface *surf)
    : wayfire_surface_t (surf, NULL)
{
    set_output(core->get_active_output());
    output->render->schedule_redraw();

    surface = surf;

    geometry.x = geometry.y = 0;
    geometry.width = surface->current->width;
    geometry.height = surface->current->height;

    transform.color = glm::vec4(1, 1, 1, 1);
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

wayfire_surface_t *wayfire_view_t::map_input_coordinates(int cx, int cy, int& sx, int& sy)
{
    wayfire_surface_t *ret = NULL;

    for_each_surface(
        [&] (wayfire_surface_t *surface, int x, int y)
        {
            if (ret) return;

            sx = cx - x;
            sy = cy - y;

            if (wlr_surface_point_accepts_input(surface->surface, sx, sy))
                ret = surface;
        });

    return ret;
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
    wayfire_surface_t::commit();
    update_size();
}

void wayfire_view_t::damage()
{
    /* TODO: bounding box damage */
    output->render->damage(get_output_geometry());
}

/* common desktop-api functions */
static inline void handle_move_request(wayfire_view view)
{
    move_request_signal data;
    data.view = view;
    view->get_output()->emit_signal("move-request", &data);
}

static inline void handle_resize_request(wayfire_view view)
{
    resize_request_signal data;
    data.view = view;
    view->get_output()->emit_signal("resize-request", &data);
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
        view->get_output()->emit_signal("view-maximized-request", &data);
    } else if (state)
    {
        view->set_geometry(view->get_output()->workspace->get_workarea());
        view->get_output()->emit_signal("view-maximized", &data);
    }
}

void handle_fullscreen_request(wayfire_view view, wayfire_output *output, bool state)
{
    if (!view || view->fullscreen == state)
        return;

    auto wo = (output ? output : view->get_output());
    wo = (wo ? wo : core->get_active_output());
    assert(wo);

    if (view->get_output() != wo)
    {
        auto pg = view->get_output()->get_full_geometry();
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
        view->set_geometry(view->get_output()->get_full_geometry());
        view->get_output()->emit_signal("view-fullscreen", &data);
    }

    view->set_fullscreen(state);
}

/* xdg_shell_v6 implementation */
/* TODO: unmap */

void handle_new_popup(wl_listener*, void*);
void handle_popup_map(wl_listener*, void*);
void handle_popup_unmap(wl_listener*, void*);
void handle_popup_destroy(wl_listener*, void*);

/* xdg_popup_v6 implementation
 * Currently we use a "hack": we treat the toplevel as a special popup,
 * so that we can use the same functions for adding a new popup, tracking them, etc. */

/* TODO: Figure out a way to animate this */
class wayfire_xdg6_popup : public wayfire_surface_t
{
    protected:
        wl_listener new_popup, destroy_popup,
                    m_popup_map, m_popup_unmap;

        wlr_xdg_popup_v6 *popup;
        wlr_xdg_surface_v6 *xdg_surface;

    public:
        wayfire_xdg6_popup(wlr_xdg_popup_v6 *popup)
            :wayfire_surface_t(popup->base->surface,
                               core->api->desktop_surfaces[popup->parent->surface])
        {
            assert(parent_surface);
            this->popup = popup;

            new_popup.notify     = handle_new_popup;
            destroy_popup.notify = handle_popup_destroy;
            m_popup_map.notify   = handle_popup_map;
            m_popup_unmap.notify = handle_popup_unmap;

            wl_signal_add(&popup->base->events.new_popup, &new_popup);
            wl_signal_add(&popup->base->events.destroy,   &destroy_popup);
            wl_signal_add(&popup->base->events.map,       &m_popup_map);
            wl_signal_add(&popup->base->events.unmap,     &m_popup_unmap);
        }

        virtual void get_child_position(int &x, int &y)
        {
            double sx, sy;
            wlr_xdg_surface_v6_popup_get_position(popup->base, &sx, &sy);
            x = sx; y = sy;
        }
};

void handle_new_popup(wl_listener*, void *data)
{
    auto popup = static_cast<wlr_xdg_popup_v6*> (data);
    auto it = core->api->desktop_surfaces.find(popup->parent->surface);
    if (it == core->api->desktop_surfaces.end())
    {
        log_error("attempting to create a popup with unknown parent");
        return;
    }

    new wayfire_xdg6_popup(popup);
}

/* TODO: damage from popups, recursive till top */
void handle_popup_map(wl_listener*, void *data)
{
    auto popup = static_cast<wlr_xdg_surface_v6*> (data);
    auto it = core->api->desktop_surfaces.find(popup->surface);
    if (it == core->api->desktop_surfaces.end())
    {
        log_error("attempting to map an unknown popup");
        return;
    }

    it->second->is_mapped = true;
}

void handle_popup_unmap(wl_listener*, void *data)
{
    auto popup = static_cast<wlr_xdg_surface_v6*> (data);
    auto it = core->api->desktop_surfaces.find(popup->surface);
    if (it == core->api->desktop_surfaces.end())
    {
        log_error("attempting to unmap an unknown popup");
        return;
    }

    it->second->is_mapped = false;
}

void handle_popup_destroy(wl_listener*, void *data)
{
    auto popup = static_cast<wlr_xdg_surface_v6*> (data);
    auto it = core->api->desktop_surfaces.find(popup->surface);
    if (it == core->api->desktop_surfaces.end())
    {
        log_error("attempting to destroy an unknown popup");
        return;
    }

    delete it->second;
}

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

class wayfire_xdg6_view : public wayfire_view_t
{
    wlr_xdg_surface_v6 *v6_surface;
    wl_listener map,
                request_move, request_resize,
                request_maximize, request_fullscreen;


    public:
    wayfire_xdg6_view(wlr_xdg_surface_v6 *s)
        : wayfire_view_t (s->surface), v6_surface(s)
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

    wf_point get_output_position()
    {
        return {
            geometry.x - v6_surface->geometry.x,
            geometry.y - v6_surface->geometry.y,
        };
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
        wayfire_view_t::activate(act);
        wlr_xdg_toplevel_v6_set_activated(v6_surface, act);
    }

    void set_maximized(bool max)
    {
        wayfire_view_t::set_maximized(max);
        wlr_xdg_toplevel_v6_set_maximized(v6_surface, max);
    }

    void set_fullscreen(bool full)
    {
        wayfire_view_t::set_fullscreen(full);
        wlr_xdg_toplevel_v6_set_fullscreen(v6_surface, full);
    }

    void move(int w, int h, bool send)
    {
        wayfire_view_t::move(w, h, send);
    }

    void resize(int w, int h, bool send)
    {
        wayfire_view_t::resize(w, h, send);
        wlr_xdg_toplevel_v6_set_size(v6_surface, w, h);
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
    handle_fullscreen_request(view, view->get_output(), surf->fullscreen);
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


