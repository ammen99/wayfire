#include "debug.hpp"
#include "core.hpp"
#include "opengl.hpp"
#include "output.hpp"
#include "view.hpp"
#include "view-transform.hpp"
#include "decorator.hpp"
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
#include <wlr/util/region.h>
#undef static
}

/* TODO: clean up the code, currently it is a horrible mess */

/* misc definitions */

/* TODO: use surface->data instead of a global map */

bool operator == (const wf_geometry& a, const wf_geometry& b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool operator != (const wf_geometry& a, const wf_geometry& b)
{
    return !(a == b);
}

/* TODO: implement rotation */
wf_geometry get_output_box_from_box(const wf_geometry& g, float scale, wl_output_transform)
{
    wf_geometry r;
    r.x = std::floor(g.x * scale);
    r.y = std::floor(g.y * scale);
    r.width = std::ceil(g.width * scale);
    r.height = std::ceil(g.height * scale);

    return r;
}

wf_point operator + (const wf_point& a, const wf_point& b)
{
    return {a.x + b.x, a.y + b.y};
}

wf_point operator + (const wf_point& a, const wf_geometry& b)
{
    return {a.x + b.x, a.y + b.y};
}

wf_geometry operator + (const wf_geometry &a, const wf_point& b)
{
    return {
        a.x + b.x,
        a.y + b.y,
        a.width,
        a.height
    };
}

wf_point operator - (const wf_point& a)
{
    return {-a.x, -a.y};
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

static wayfire_surface_t* wf_surface_from_void(void *handle)
{
    return static_cast<wayfire_surface_t*> (handle);
}

static wayfire_view_t* wf_view_from_void(void *handle)
{
    return dynamic_cast<wayfire_view_t*> (wf_surface_from_void(handle));
}

wayfire_view wl_surface_to_wayfire_view(wl_resource *resource)
{
    auto surface = (wlr_surface*) wl_resource_get_user_data(resource);

    void *handle = NULL;

    if (wlr_surface_is_xdg_surface_v6(surface))
        handle = wlr_xdg_surface_v6_from_wlr_surface(surface)->data;

    if (wlr_surface_is_xwayland_surface(surface))
        handle = wlr_xwayland_surface_from_wlr_surface(surface)->data;

    return core->find_view(wf_surface_from_void(handle));
}

/* wayfire_surface_t implementation */
void handle_surface_committed(wl_listener*, void *data)
{
    auto wlr_surf = (wlr_surface*) data;
    auto surface = wf_surface_from_void(wlr_surf->data);
    assert(surface);

    surface->commit();
}

void handle_subsurface_created(wl_listener*, void *data)
{
    auto sub = static_cast<wlr_subsurface*> (data);
    if (sub->surface->data)
        return;

    auto parent = wf_surface_from_void(sub->parent->data);
    if (!parent)
    {
        log_error("subsurface created with invalid parent!");
        return;
    }

    auto surf = new wayfire_surface_t(parent);
    surf->map(sub->surface);
}

void handle_subsurface_destroyed(wl_listener*, void *data)
{
    auto wlr_surf = (wlr_surface*) data;
    auto surface = wf_surface_from_void(wlr_surf->data);

    surface->unmap();
    surface->dec_keep_count();
}

wayfire_surface_t::wayfire_surface_t(wayfire_surface_t* parent)
{
    inc_keep_count();
    this->parent_surface = parent;

    if (parent)
    {
        set_output(parent->output);
        parent->surface_children.push_back(this);
    }

    new_sub.notify   = handle_subsurface_created;
    committed.notify = handle_surface_committed;
    destroy.notify   = nullptr;
}

wayfire_surface_t::~wayfire_surface_t()
{
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
    {
        /* if we are a decoration window, then we shouldn't
         * destroy children(contained view) like that */
        auto cast = dynamic_cast<wayfire_view_t*> (c);
        if (!cast)
            c->destruct();
    }
}

wayfire_surface_t *wayfire_surface_t::get_main_surface()
{
    return this->parent_surface ? this->parent_surface->get_main_surface() : this;
}

bool wayfire_surface_t::is_subsurface()
{
    return wlr_surface_is_subsurface(surface);
}

void wayfire_surface_t::get_child_position(int &x, int &y)
{
    x = surface->current->subsurface_position.x;
    y = surface->current->subsurface_position.y;
}

wf_point wayfire_surface_t::get_output_position()
{
    auto pos = parent_surface->get_output_position();

    int dx, dy;
    get_child_position(dx, dy);
    pos.x += dx; pos.y += dy;

    return pos;
}

wf_geometry wayfire_surface_t::get_output_geometry()
{
    if (!is_mapped())
        return {0, 0, 0, 0};

    auto pos = get_output_position();
    return {
        pos.x, pos.y,
        surface->current ? surface->current->width : 0,
        surface->current ? surface->current->height : 0
    };
}

void wayfire_surface_t::map(wlr_surface *surface)
{
    assert(!this->surface && surface);
    this->surface = surface;

    wl_signal_add(&surface->events.new_subsurface, &new_sub);
    wl_signal_add(&surface->events.commit,         &committed);

    /* map by default if this is a subsurface, only toplevels/popups have map/unmap events */
    if (wlr_surface_is_subsurface(surface))
    {
        destroy.notify = handle_subsurface_destroyed;
        wl_signal_add(&surface->events.destroy, &destroy);
    }

    surface->data = this;
    damage();
}

void wayfire_surface_t::unmap()
{
    assert(this->surface);
    damage();

    this->surface = nullptr;

    wl_list_remove(&new_sub.link);
    wl_list_remove(&committed.link);
    if (destroy.notify)
        wl_list_remove(&destroy.link);
}

void wayfire_surface_t::damage(pixman_region32_t *region)
{
    int n_rect;
    auto rects = pixman_region32_rectangles(region, &n_rect);

    for (int i = 0; i < n_rect; i++)
        damage({rects[i].x1, rects[i].y1, rects[i].x2 - rects[i].x1, rects[i].y2 - rects[i].y1});
}

void wayfire_surface_t::damage(const wlr_box& box)
{
    assert(parent_surface);
    parent_surface->damage(box);
}

void wayfire_surface_t::damage()
{
    /* TODO: bounding box damage */
    damage(geometry);
}

void wayfire_surface_t::commit()
{
    wf_geometry rect = get_output_geometry();

    pixman_region32_t dmg;

    pixman_region32_init(&dmg);
    pixman_region32_copy(&dmg, &surface->current->surface_damage);
    pixman_region32_translate(&dmg, rect.x, rect.y);

    /* TODO: recursively damage children? */
    if (is_subsurface() && rect != geometry)
    {
        damage(geometry);
        damage(rect);

        geometry = rect;
    }

    /* TODO: transform damage */
    damage(&dmg);
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
    {
        if (is_mapped())
            call(this, x, y);

        int dx, dy;

        for (auto c : surface_children)
        {
            if (!c->is_mapped())
                continue;

            c->get_child_position(dx, dy);
            c->for_each_surface_recursive(call, x + dx, y + dy, reverse);
        }
    } else
    {
        auto it = surface_children.rbegin();
        int dx, dy;

        while(it != surface_children.rend())
        {
            auto& c = *it;

            if (c->is_mapped())
            {
                c->get_child_position(dx, dy);
                c->for_each_surface_recursive(call, x + dx, y + dy, reverse);
            }

            ++it;
        }

        if (is_mapped())
            call(this, x, y);
    }
}

void wayfire_surface_t::for_each_surface(wf_surface_iterator_callback call, bool reverse)
{
    auto pos = get_output_position();
    for_each_surface_recursive(call, pos.x, pos.y, reverse);
}

void wayfire_surface_t::render_fbo(int x, int y, int fb_w, int fb_h,
                                   pixman_region32_t *damage)
{
    if (!wlr_surface_has_buffer(surface))
        return;

    /* TODO: optimize, use offscreen_buffer.cached_damage */
    wlr_box fb_geometry;

    fb_geometry.x = x; fb_geometry.y = y;
    fb_geometry.width = surface->current->width;
    fb_geometry.height = surface->current->height;

    float id[9];
    wlr_matrix_projection(id, fb_w, fb_h, WL_OUTPUT_TRANSFORM_NORMAL);

    float matrix[9];
    wlr_matrix_project_box(matrix, &fb_geometry, WL_OUTPUT_TRANSFORM_NORMAL, 0, id);

    wlr_matrix_scale(matrix, 1.0 / fb_geometry.width, 1.0 / fb_geometry.height);

    wlr_renderer_scissor(core->renderer, NULL);
    wlr_render_texture(core->renderer, surface->texture, matrix, 0, 0, alpha);
}

static wlr_box get_scissor_box(wayfire_output *output, wlr_box *box)
{
    int ow, oh;
    wlr_output_transformed_resolution(output->handle, &ow, &oh);

    wlr_box result;
    memcpy(&result, box, sizeof(result));

    enum wl_output_transform transform = wlr_output_transform_invert(output->handle->transform);
    wlr_box_transform(box, transform, ow, oh, &result);
    return result;
}

void wayfire_surface_t::render(int x, int y, wlr_box *damage)
{
    if (!wlr_surface_has_buffer(surface))
        return;

    wlr_box geometry {x, y, surface->current->width, surface->current->height};
    geometry = get_output_box_from_box(geometry, output->handle->scale,
                                       WL_OUTPUT_TRANSFORM_NORMAL);

    if (!damage) damage = &geometry;

    auto rr = core->renderer;
    float matrix[9];
    wlr_matrix_project_box(matrix, &geometry,
                           surface->current->transform,
                           0, output->handle->transform_matrix);

    auto box = get_scissor_box(output, damage);
    wlr_renderer_scissor(rr, &box);

    wlr_render_texture_with_matrix(rr, surface->texture, matrix, alpha);
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

void wayfire_surface_t::render_fb(int x, int y, pixman_region32_t *damage, int fb)
{
    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb));
    render_pixman(x, y, damage);
}

/* wayfire_view_t implementation */
uint32_t _last_view_id = 0;
wayfire_view_t::wayfire_view_t()
    : wayfire_surface_t (NULL), id(_last_view_id++)
{
    set_output(core->get_active_output());
    pixman_region32_init(&offscreen_buffer.cached_damage);
}

wayfire_view wayfire_view_t::self()
{
    return core->find_view((wayfire_surface_t*) this);
}

// TODO: implement is_visible
bool wayfire_view_t::is_visible()
{
    return true;
}

bool wayfire_view_t::update_size()
{
    assert(surface);

    int old_w = geometry.width, old_h = geometry.height;

    geometry.width = surface->current ? surface->current->width  : 0;
    geometry.height = surface->current? surface->current->height : 0;

    return geometry.width != old_w || geometry.height != old_h;
}

void wayfire_view_t::set_moving(bool moving)
{
    in_continuous_move += moving ? 1 : -1;
    if (decoration)
        decoration->set_moving(moving);
}

void wayfire_view_t::set_resizing(bool resizing)
{
    in_continuous_resize += resizing ? 1 : -1;
    if (decoration)
        decoration->set_resizing(resizing);
}

void wayfire_view_t::move(int x, int y, bool send_signal)
{
    view_geometry_changed_signal data;
    data.view = self();
    data.old_geometry = get_wm_geometry();

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
    data.view = self();
    data.old_geometry = get_wm_geometry();

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

    auto wm = get_wm_geometry();
    int center_x = wm.x + wm.width / 2;
    int center_y = wm.y + wm.height / 2;

    for_each_surface(
        [&] (wayfire_surface_t *surface, int x, int y)
        {
            if (ret) return;

            int lx = cx - center_x,
                ly = center_y - cy;

            if (transform)
            {
                auto transformed = transform->transformed_to_local_point({lx, ly});
                lx = transformed.x;
                ly = transformed.y;
            }

            lx = lx + center_x;
            ly = center_y - ly;

            sx = lx - x;
            sy = ly - y;

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

wf_geometry wayfire_view_t::get_bounding_box()
{
    if (!transform)
        return get_output_geometry();

    wlr_box wm = get_wm_geometry();
    wlr_box box = get_output_geometry();

    box.x = (box.x - wm.x) - wm.width / 2;
    box.y = wm.height / 2 - (box.y - wm.y);

    box = transform->get_bounding_box(box);

    box.x = box.x + wm.x + wm.width / 2;
    box.y = (wm.height / 2 - box.y) + wm.y;

    return box;
}

void wayfire_view_t::set_maximized(bool maxim)
{
    maximized = maxim;
}

void wayfire_view_t::set_fullscreen(bool full)
{
    fullscreen = full;
}

void wayfire_view_t::activate(bool active)
{ }

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

void wayfire_view_t::get_child_position(int &x, int &y)
{
    assert(decoration);

    x = decor_x;
    y = decor_y;
}

wayfire_surface_t* wayfire_view_t::get_main_surface()
{
    if (decoration)
        return decoration->get_main_surface();
    return this;
}

wf_point wayfire_view_t::get_output_position()
{
    if (decoration)
        return decoration->get_output_position() + wf_point{decor_x, decor_y};
    return wf_point{geometry.x, geometry.y};
}

void wayfire_view_t::damage(const wlr_box& box)
{
    if (decoration)
        return decoration->damage(box);

    auto wm_geometry = get_wm_geometry();
    if (transform)
    {
        auto real_box = box;
        real_box.x -= wm_geometry.x;
        real_box.y -= wm_geometry.y;

        pixman_region32_union_rect(&offscreen_buffer.cached_damage,
                                   &offscreen_buffer.cached_damage,
                                   real_box.x, real_box.y,
                                   real_box.width, real_box.height);

        /* TODO: damage only the bounding box of region */
        output->render->damage(get_output_box_from_box(get_bounding_box(),
                                                       output->handle->scale,
                                                       WL_OUTPUT_TRANSFORM_NORMAL));
    } else
    {
        output->render->damage(get_output_box_from_box(box,
                                                       output->handle->scale,
                                                       WL_OUTPUT_TRANSFORM_NORMAL));
    }
}

static wf_geometry get_output_centric_geometry(const wf_geometry& output, wf_geometry view)
{
    view.x = view.x - output.width / 2;
    view.y = output.height /2 - view.y;

    return view;
}

void wayfire_view_t::render_fb(int x, int y, pixman_region32_t* damage, int fb)
{
    if (!wlr_surface_has_buffer(surface))
        return;

    //log_info("render_pixman %p", surface);

    if (decoration && decoration->get_transformer())
        return;

    if (transform && !decoration)
    {
        auto output_geometry = get_output_geometry();

        int scale = surface->current->scale;
        if (output_geometry.width * scale != offscreen_buffer.fb_width
            || output_geometry.height * scale != offscreen_buffer.fb_height)
        {
            if (offscreen_buffer.fbo != (uint)-1)
            {
                glDeleteFramebuffers(1, &offscreen_buffer.fbo);
                glDeleteTextures(1, &offscreen_buffer.tex);

                offscreen_buffer.fbo = offscreen_buffer.tex = -1;
            }
        }

        if (offscreen_buffer.fbo == (uint32_t)-1)
        {
            pixman_region32_init(&offscreen_buffer.cached_damage);
            OpenGL::prepare_framebuffer_size(output_geometry.width * scale, output_geometry.height * scale,
                                             offscreen_buffer.fbo, offscreen_buffer.tex);

            offscreen_buffer.fb_width = output_geometry.width * scale;
            offscreen_buffer.fb_height = output_geometry.height * scale;

            GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, offscreen_buffer.fbo));
            GL_CALL(glViewport(0, 0, output_geometry.width, output_geometry.height));

            wlr_renderer_scissor(core->renderer, NULL);

            GL_CALL(glClearColor(1, 1, 1, 0));
            GL_CALL(glClear(GL_COLOR_BUFFER_BIT));
        }

        for_each_surface([=] (wayfire_surface_t *surface, int x, int y) {
            GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, offscreen_buffer.fbo));
            GL_CALL(glViewport(0, 0, offscreen_buffer.fb_width, offscreen_buffer.fb_height));
            surface->render_fbo((x - output_geometry.x) * scale, (y - output_geometry.y) * scale,
                                offscreen_buffer.fb_width, offscreen_buffer.fb_height,
                                NULL);
        }, true);

        auto obox = output_geometry;
        obox.x = x;
        obox.y = y;
        auto centric_geometry = get_output_centric_geometry(output->get_full_geometry(), obox);

        int n_rect;
        auto rects = pixman_region32_rectangles(damage, &n_rect);

        for (int i = 0; i < n_rect; i++)
        {
            auto box = wlr_box{rects[i].x1, rects[i].y1,
                rects[i].x2 - rects[i].x1, rects[i].y2 - rects[i].y1};

            auto sbox = get_scissor_box(output, &box);
            transform->render_with_damage(offscreen_buffer.tex, fb, centric_geometry, sbox);
        }
    } else
    {
        wayfire_surface_t::render_fb(x, y, damage, fb);
    }
}

void wayfire_view_t::set_transformer(std::unique_ptr<wf_view_transformer_t> transformer)
{
    /* TODO: damage all */
    transform = std::move(transformer);
}

void wayfire_view_t::map(wlr_surface *surface)
{
    wayfire_surface_t::map(surface);

    if (!is_special)
    {
        auto workarea = output->workspace->get_workarea();
        geometry.x += workarea.x;
        geometry.y += workarea.y;
    }

    if (update_size())
        damage();

    /* TODO: consider not emitting a create-view for special surfaces */
    map_view_signal data;
    data.view = self();
    output->emit_signal("map-view", &data);

    if (!is_special)
    {
        output->attach_view(self());
        output->focus_view(self());
    }
}

void wayfire_view_t::unmap()
{
    wayfire_surface_t::unmap();

    if (decoration)
    {
        decoration->close();
        decoration->unmap();
    }

    /* TODO: what if a plugin wants to keep this view? */
    auto old_output = output;
    output->detach_view(self());
    output = old_output;

    unmap_view_signal data;
    data.view = self();
    output->emit_signal("unmap-view", &data);
}

void wayfire_view_t::move_request()
{
    if (decoration)
        return decoration->move_request();

    move_request_signal data;
    data.view = self();
    output->emit_signal("move-request", &data);
}

void wayfire_view_t::resize_request()
{
    if (decoration)
        return decoration->resize_request();

    resize_request_signal data;
    data.view = self();
    output->emit_signal("resize-request", &data);
}

void wayfire_view_t::maximize_request(bool state)
{
    if (decoration)
        return decoration->maximize_request(state);

    if (maximized == state)
        return;

    view_maximized_signal data;
    data.view = self();
    data.state = state;

    if (surface)
    {
        output->emit_signal("view-maximized-request", &data);
    } else if (state)
    {
        set_geometry(output->workspace->get_workarea());
        output->emit_signal("view-maximized", &data);
    }
}

void wayfire_view_t::fullscreen_request(wayfire_output *out, bool state)
{
    if (decoration)
        return decoration->fullscreen_request(out, state);

    if (fullscreen == state)
        return;

    auto wo = (out ? out : (output ? output : core->get_active_output()));
    assert(wo);

    if (output != wo)
    {
        //auto pg = view->get_output()->get_full_geometry();
        //auto ng = wo->get_full_geometry();

        core->move_view_to_output(self(), wo);
        /* TODO: check if we really need global coordinates or just output-local */
       // view->move(view->geometry.x + ng.x - pg.x, view->geometry.y + ng.y - pg.y);
    }

    view_fullscreen_signal data;
    data.view = self();
    data.state = state;

    if (surface) {
        wo->emit_signal("view-fullscreen-request", &data);
    } else if (state) {
        set_geometry(output->get_full_geometry());
        output->emit_signal("view-fullscreen", &data);
    }

    set_fullscreen(state);
}

/* xdg_shell_v6 implementation */
/* TODO: unmap */

static void handle_new_popup(wl_listener*, void*);
static void handle_v6_map(wl_listener*, void *data);
static void handle_v6_unmap(wl_listener*, void *data);
static void handle_v6_destroy(wl_listener*, void *data);

/* xdg_popup_v6 implementation
 * Currently we use a "hack": we treat the toplevel as a special popup,
 * so that we can use the same functions for adding a new popup, tracking them, etc. */

/* TODO: Figure out a way to animate this */
class wayfire_xdg6_popup : public wayfire_surface_t
{
    protected:
        wl_listener destroy,
                    new_popup, destroy_popup,
                    m_popup_map, m_popup_unmap;

        wlr_xdg_popup_v6 *popup;
        wlr_xdg_surface_v6 *xdg_surface;

    public:
        wayfire_xdg6_popup(wlr_xdg_popup_v6 *popup)
            :wayfire_surface_t(wf_surface_from_void(popup->parent->surface->data))
        {
            assert(parent_surface);
            log_info("new xdg6 popup");
            this->popup = popup;

            destroy.notify       = handle_v6_destroy;
            new_popup.notify     = handle_new_popup;
            m_popup_map.notify   = handle_v6_map;
            m_popup_unmap.notify = handle_v6_unmap;

            wl_signal_add(&popup->base->events.new_popup, &new_popup);
            wl_signal_add(&popup->base->events.map,       &m_popup_map);
            wl_signal_add(&popup->base->events.unmap,     &m_popup_unmap);
            wl_signal_add(&popup->base->events.destroy,   &destroy);

            popup->base->data = this;
        }

        virtual void get_child_position(int &x, int &y)
        {
            double sx, sy;
            wlr_xdg_surface_v6_popup_get_position(popup->base, &sx, &sy);
            x = sx; y = sy;
        }

        virtual bool is_subsurface() { return true; }
};

void handle_new_popup(wl_listener*, void *data)
{
    auto popup = static_cast<wlr_xdg_popup_v6*> (data);
    auto parent = wf_surface_from_void(popup->parent->surface->data);
    if (!parent)
    {
        log_error("attempting to create a popup with unknown parent");
        return;
    }

    new wayfire_xdg6_popup(popup);
}

static void handle_v6_map(wl_listener*, void *data)
{
    auto surface = static_cast<wlr_xdg_surface_v6*> (data);
    auto wf_surface = wf_surface_from_void(surface->data);

    assert(wf_surface);
    wf_surface->map(surface->surface);
}

static void handle_v6_unmap(wl_listener*, void *data)
{
    auto surface = static_cast<wlr_xdg_surface_v6*> (data);
    auto wf_surface = wf_surface_from_void(surface->data);

    assert(wf_surface);
    wf_surface->unmap();
}

static void handle_v6_destroy(wl_listener*, void *data)
{
    auto surface = static_cast<wlr_xdg_surface_v6*> (data);
    auto wf_surface = wf_surface_from_void(surface->data);

    assert(wf_surface);
    wf_surface->destroyed = 1;
    wf_surface->dec_keep_count();
}

static void handle_v6_request_move(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_xdg_toplevel_v6_move_event*> (data);
    auto view = wf_view_from_void(ev->surface->data);
    view->move_request();
}

static void handle_v6_request_resize(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_xdg_toplevel_v6_resize_event*> (data);
    auto view = wf_view_from_void(ev->surface->data);
    view->resize_request();
}

static void handle_v6_request_maximized(wl_listener*, void *data)
{
    auto surf = static_cast<wlr_xdg_surface_v6*> (data);
    auto view = wf_view_from_void(surf->data);
    view->maximize_request(surf->toplevel->client_pending.maximized);
}

static void handle_v6_request_fullscreen(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_xdg_toplevel_v6_set_fullscreen_event*> (data);
    auto view = wf_view_from_void(ev->surface->data);
    auto wo = core->get_output(ev->output);
    view->fullscreen_request(wo, ev->fullscreen);
}

/* TODO: perhaps implement show_window_menu and minimize_request if anyone needs those */
class wayfire_xdg6_view : public wayfire_view_t
{
    protected:
        wl_listener destroy, map_ev, unmap, new_popup,
                request_move, request_resize,
                request_maximize, request_fullscreen;


    public:
        wlr_xdg_surface_v6 *v6_surface;

    wayfire_xdg6_view(wlr_xdg_surface_v6 *s)
        : wayfire_view_t(), v6_surface(s)
    {
        log_info ("new xdg_shell_v6 surface: %s app-id: %s",
                  nonull(v6_surface->toplevel->title),
                  nonull(v6_surface->toplevel->app_id));

        destroy.notify            = handle_v6_destroy;
        new_popup.notify          = handle_new_popup;
        map_ev.notify             = handle_v6_map;
        unmap.notify              = handle_v6_unmap;
        request_move.notify       = handle_v6_request_move;
        request_resize.notify     = handle_v6_request_resize;
        request_maximize.notify   = handle_v6_request_maximized;
        request_fullscreen.notify = handle_v6_request_fullscreen;

        wlr_xdg_surface_v6_ping(s);

        wl_signal_add(&v6_surface->events.destroy, &destroy);
        wl_signal_add(&s->events.new_popup,        &new_popup);
        wl_signal_add(&v6_surface->events.map,     &map_ev);
        wl_signal_add(&v6_surface->events.unmap,   &unmap);
        wl_signal_add(&v6_surface->toplevel->events.request_move,       &request_move);
        wl_signal_add(&v6_surface->toplevel->events.request_resize,     &request_resize);
        wl_signal_add(&v6_surface->toplevel->events.request_maximize,   &request_maximize);
        wl_signal_add(&v6_surface->toplevel->events.request_fullscreen, &request_fullscreen);

        v6_surface->data = this;
    }

    virtual void map(wlr_surface *surface)
    {
        wayfire_view_t::map(surface);

        log_info("map surface, maximized is %d", v6_surface->toplevel->current.maximized);
        if (v6_surface->toplevel->client_pending.maximized)
            maximize_request(true);

        if (v6_surface->toplevel->client_pending.fullscreen)
            fullscreen_request(output, true);
    }

    virtual wf_point get_output_position()
    {
        if (decoration)
            return decoration->get_output_position()
                + wf_point{decor_x, decor_y}
                + wf_point{-v6_surface->geometry.x, -v6_surface->geometry.y};

        wf_point position {
            geometry.x - v6_surface->geometry.x,
            geometry.y - v6_surface->geometry.y,
        };

        return position;
    }

    virtual void get_child_position(int &x, int &y)
    {
        assert(decoration);

        x = decor_x - v6_surface->geometry.x;
        y = decor_y - v6_surface->geometry.y;
    }

    virtual bool update_size()
    {
        auto old_w = geometry.width, old_h = geometry.height;
        if (v6_surface->geometry.width > 0 && v6_surface->geometry.height > 0)
        {
            geometry.width = v6_surface->geometry.width;
            geometry.height = v6_surface->geometry.height;
        } else
        {
            wayfire_view_t::update_size();
        }

        return old_w != geometry.width || old_h != geometry.height;
    }

    virtual void activate(bool act)
    {
        wayfire_view_t::activate(act);
        wlr_xdg_toplevel_v6_set_activated(v6_surface, act);
    }

    virtual void set_maximized(bool max)
    {
        wayfire_view_t::set_maximized(max);
        wlr_xdg_toplevel_v6_set_maximized(v6_surface, max);
    }

    virtual void set_fullscreen(bool full)
    {
        wayfire_view_t::set_fullscreen(full);
        wlr_xdg_toplevel_v6_set_fullscreen(v6_surface, full);
    }

    virtual void move(int w, int h, bool send)
    {
        wayfire_view_t::move(w, h, send);
    }

    virtual void resize(int w, int h, bool send)
    {
        wayfire_view_t::resize(w, h, send);
        wlr_xdg_toplevel_v6_set_size(v6_surface, w, h);
    }

    std::string get_app_id()
    {
        return nonull(v6_surface->toplevel->app_id);
    }

    std::string get_title()
    {
        return nonull(v6_surface->toplevel->title);
    }

    virtual void close()
    {
        wlr_xdg_surface_v6_send_close(v6_surface);
    }

    ~wayfire_xdg6_view()
    {
    }
};

/* end of xdg_shell_v6 implementation */

/* start xdg6_decoration implementation */

class wayfire_xdg6_decoration_view : public wayfire_xdg6_view
{
    wayfire_view contained = NULL;
    std::unique_ptr<wf_decorator_frame_t> frame;

    wf_point v6_surface_offset;

    public:

    wayfire_xdg6_decoration_view(wlr_xdg_surface_v6 *decor) :
        wayfire_xdg6_view(decor)
    { }

    void init(wayfire_view view, std::unique_ptr<wf_decorator_frame_t>&& fr)
    {
        frame = std::move(fr);
        contained = view;
        geometry = view->get_wm_geometry();

        set_geometry(geometry);
        surface_children.push_back(view.get());

        v6_surface_offset = {v6_surface->geometry.x, v6_surface->geometry.y};
    }

    void map(wlr_surface *surface)
    {
        wayfire_xdg6_view::map(surface);

        if (contained->maximized)
            maximize_request(true);

        if (contained->fullscreen)
            fullscreen_request(output, true);
    }

    void activate(bool state)
    {
        wayfire_xdg6_view::activate(state);
        contained->activate(state);
    }

    void commit()
    {
        wayfire_xdg6_view::commit();

        wf_point new_offset = {v6_surface->geometry.x, v6_surface->geometry.y};
        if (new_offset.x != v6_surface_offset.x || new_offset.y != v6_surface_offset.y)
        {
            move(geometry.x, geometry.y, false);
            v6_surface_offset = new_offset;
        }
    }

    void move(int x, int y, bool ss)
    {
        auto new_g = frame->get_child_geometry(geometry);
        new_g.x += v6_surface->geometry.x;
        new_g.y += v6_surface->geometry.y;

        log_info ("contained is moved to %d+%d, decor to %d+%d", new_g.x, new_g.y,
                  x, y);

        contained->decor_x = new_g.x - geometry.x;
        contained->decor_y = new_g.y - geometry.y;

        contained->move(new_g.x, new_g.y, false);
        wayfire_xdg6_view::move(x, y, ss);
    }

    void resize(int w, int h, bool ss)
    {
        auto new_geometry = geometry;
        new_geometry.width = w;
        new_geometry.height = h;

        auto new_g = frame->get_child_geometry(new_geometry);
        log_info ("contained is resized to %dx%d, decor to %dx%d", new_g.width, new_g.height,
                  w, h);

        contained->resize(new_g.width, new_g.height, false);
    }

    void child_configured(wf_geometry g)
    {
        auto new_g = frame->get_geometry_interior(g);
        /*
        log_info("contained configured %dx%d, we become: %dx%d",
                 g.width, g.height, new_g.width, new_g.height);
                 */
        if (new_g.width != geometry.width || new_g.height != geometry.height)
            wayfire_xdg6_view::resize(new_g.width, new_g.height, false);
    }

    void unmap()
    {
        /* if the contained view was closed earlier, then the decoration view
         * has already been forcibly unmapped */
        if (!surface) return;

        wayfire_view_t::unmap();

        if (contained->is_mapped())
        {
            contained->set_decoration(nullptr, nullptr);
            contained->close();
        }
    }

    wlr_surface *get_keyboard_focus_surface()
    { return contained->get_keyboard_focus_surface(); }

    void set_maximized(bool state)
    {
        wayfire_xdg6_view::set_maximized(state);
        contained->set_maximized(state);
    }

    void set_fullscreen(bool state)
    {
        wayfire_xdg6_view::set_fullscreen(state);
        contained->set_fullscreen(state);
    }

 //   void move_request() { contained->move_request(); }
  //  void resize_request() { contained->resize_request(); }
   // void maximize_request(bool state) { contained->maximize_request(state); }
   // void fullscreen_request(wayfire_output *wo, bool state)
    //{ contained->fullscreen_request(wo, state); }

    /* TODO: fullscreen ?
    void set_fullscreen(wayfire_output *wo, bool state)
    {
        _set_fullscreen(wo, state);
    }
    */

    ~wayfire_xdg6_decoration_view()
    { }
};

void wayfire_view_t::commit()
{
    wayfire_surface_t::commit();

    auto old = get_output_geometry();
    if (update_size())
    {
        damage(old);
        damage();
    }

    /* configure frame_interior */
    if (decoration)
    {
        auto decor = std::dynamic_pointer_cast<wayfire_xdg6_decoration_view> (decoration);
        assert(decor);
        decor->child_configured(geometry);
    }
}

void wayfire_view_t::damage()
{
    damage(get_bounding_box());
}


void wayfire_view_t::destruct()
{
    auto cast = dynamic_cast<wayfire_xdg6_view*> (this);
    if (cast)
        log_info("destroy for self %p", cast->v6_surface);
    core->erase_view(self());
}

wayfire_view_t::~wayfire_view_t()
{
    pixman_region32_fini(&offscreen_buffer.cached_damage);
    for (auto& kv : custom_data)
        delete kv.second;
}

void wayfire_view_t::set_decoration(wayfire_view decor,
                                    std::unique_ptr<wf_decorator_frame_t> frame)
{
    if (decor)
    {
        auto raw_ptr = dynamic_cast<wayfire_xdg6_decoration_view*> (decor.get());
        assert(raw_ptr);

        if (output)
        {
            output->detach_view(self());
        }

        raw_ptr->init(self(), std::move(frame));
    }

    decoration = decor;
}

/* end xdg6_decoration_implementation */

void notify_v6_created(wl_listener*, void *data)
{
    auto surf = static_cast<wlr_xdg_surface_v6*> (data);

    if (surf->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL)
    {
        if (surf->toplevel->title &&
            core->api->decorator &&
            core->api->decorator->is_decoration_window(surf->toplevel->title))
        {
            log_info("create wf decoration view");

            auto view = std::make_shared<wayfire_xdg6_decoration_view> (surf);
            core->add_view(view);

            core->api->decorator->decoration_ready(view);
        } else
        {
            log_info("core add view for surf %p", surf);
            core->add_view(std::make_shared<wayfire_xdg6_view> (surf));
        }
    }
}

/* xwayland implementation */
static void handle_xwayland_request_move(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_xwayland_move_event*> (data);
    auto view = wf_view_from_void(ev->surface->data);
    view->move_request();
}

static void handle_xwayland_request_resize(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_xwayland_resize_event*> (data);
    auto view = wf_view_from_void(ev->surface->data);
    view->resize_request();
}

static void handle_xwayland_request_configure(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_xwayland_surface_configure_event*> (data);
    log_info("configure request");
    auto view = wf_view_from_void(ev->surface->data);
     view->set_geometry({ev->x, ev->y, ev->width, ev->height});
}

static void handle_xwayland_request_maximize(wl_listener*, void *data)
{
    auto surf = static_cast<wlr_xwayland_surface*> (data);
    auto view = wf_view_from_void(surf->data);
    view->maximize_request(surf->maximized_horz && surf->maximized_vert);
}

static void handle_xwayland_request_fullscreen(wl_listener*, void *data)
{
    auto surf = static_cast<wlr_xwayland_surface*> (data);
    auto view = wf_view_from_void(surf->data);
    view->fullscreen_request(view->get_output(), surf->fullscreen);
}

static void handle_xwayland_map(wl_listener* listener, void *data)
{
    auto xsurf = static_cast<wlr_xwayland_surface*> (data);
    auto view = wf_view_from_void(xsurf->data);

    log_info("xwayland map %p %p -> %p", xsurf, xsurf->surface, view);
    view->map(xsurf->surface);
}

static void handle_xwayland_unmap(wl_listener*, void *data)
{
    auto xsurf = static_cast<wlr_xwayland_surface*> (data);
    auto view = wf_view_from_void(xsurf->data);

    log_info("xwayland unmap %p", xsurf);
    view->unmap();
}

static void handle_xwayland_destroy(wl_listener*, void *data)
{
    auto xsurf = static_cast<wlr_xwayland_surface*> (data);
    auto view = wf_view_from_void(xsurf->data);

    log_info("xwayland destroy %p", xsurf);
    view->destroyed = 1;
    view->dec_keep_count();
}

class wayfire_xwayland_view : public wayfire_view_t
{
    wlr_xwayland_surface *xw;

    /* TODO: very bad names, also in other shells */
    wl_listener destroy, map_ev, unmap, configure,
                request_move, request_resize,
                request_maximize, request_fullscreen;

    public:
    wayfire_xwayland_view(wlr_xwayland_surface *xww)
        : wayfire_view_t(), xw(xww)
    {
        log_info("new xwayland surface %s class: %s instance: %s",
                 nonull(xw->title), nonull(xw->class_t), nonull(xw->instance));

        destroy.notify            = handle_xwayland_destroy;
        map_ev.notify             = handle_xwayland_map;
        unmap.notify              = handle_xwayland_unmap;
        configure.notify          = handle_xwayland_request_configure;
        request_move.notify       = handle_xwayland_request_move;
        request_resize.notify     = handle_xwayland_request_resize;
        request_maximize.notify   = handle_xwayland_request_maximize;
        request_fullscreen.notify = handle_xwayland_request_fullscreen;

        wl_signal_add(&xw->events.destroy,            &destroy);
        wl_signal_add(&xw->events.unmap,              &unmap);
        wl_signal_add(&xw->events.map,                &map_ev);
        wl_signal_add(&xw->events.request_move,       &request_move);
        wl_signal_add(&xw->events.request_resize,     &request_resize);
        wl_signal_add(&xw->events.request_maximize,   &request_maximize);
        wl_signal_add(&xw->events.request_fullscreen, &request_fullscreen);
        wl_signal_add(&xw->events.request_configure,  &configure);

        xw->data = this;
    }

    void map(wlr_surface *surface)
    {
        geometry.x = xw->x;
        geometry.y = xw->y;
        wayfire_view_t::map(surface);

        if (xw->maximized_horz && xw->maximized_vert)
            maximize_request(true);

        if (xw->fullscreen)
            fullscreen_request(output, true);
    }

    bool is_subsurface() { return false; }

    virtual void commit()
    {
        wayfire_view_t::commit();
        if (xw->x != geometry.x || xw->y != geometry.y)
            wayfire_view_t::move(xw->x, xw->y, false);
    }

    void activate(bool active)
    {
        wayfire_view_t::activate(active);
        wlr_xwayland_surface_activate(xw, active);
    }

    void send_configure()
    {
        wlr_xwayland_surface_configure(xw, geometry.x, geometry.y,
                                       geometry.width, geometry.height);
    }

    void move(int x, int y, bool s)
    {
        wayfire_view_t::move(x, y, s);
        send_configure();
    }

    void resize(int w, int h, bool s)
    {
        wayfire_view_t::resize(w, h, s);
        send_configure();
    }

    /* TODO: bad with decoration */
    void set_geometry(wf_geometry g)
    {
        damage();
        geometry = g;

        /* send the geometry-changed signal */
        resize(g.width, g.height, true);
        send_configure();
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

    std::string get_title() { return nonull(xw->title); }
    std::string get_app_id() {return nonull(xw->class_t); }

    void set_fullscreen(bool full)
    {
        wayfire_view_t::set_fullscreen(full);
        wlr_xwayland_surface_set_fullscreen(xw, full);
    }
};

class wayfire_unmanaged_xwayland_view : public wayfire_view_t
{
    wlr_xwayland_surface *xw;
    wl_listener destroy, unmap_listener, map_ev, configure;

    public:
    wayfire_unmanaged_xwayland_view(wlr_xwayland_surface *xww)
        : wayfire_view_t(), xw(xww)
    {
        log_info("new unmanaged xwayland surface %s class: %s instance: %s",
                 nonull(xw->title), nonull(xw->class_t), nonull(xw->instance));

        map_ev.notify         = handle_xwayland_map;
        destroy.notify        = handle_xwayland_destroy;
        unmap_listener.notify = handle_xwayland_unmap;
        configure.notify      = handle_xwayland_request_configure;

        wl_signal_add(&xw->events.destroy,            &destroy);
        wl_signal_add(&xw->events.unmap,              &unmap_listener);
        wl_signal_add(&xw->events.request_configure,  &configure);
        wl_signal_add(&xw->events.map,                &map_ev);

        xw->data = this;
    }

    bool is_subsurface() { return false; }

    void commit()
    {
        log_info("commit at %dx%d", xw->x, xw->y);
        if (geometry.x != xw->x || geometry.y != xw->y)
            wayfire_view_t::move(xw->x, xw->y, false);

        wayfire_surface_t::commit();

        auto old_geometry = geometry;
        if (update_size())
        {
            damage(old_geometry);
            damage();
        }

        log_info("geometry is %d@%d %dx%d", geometry.x, geometry.y, geometry.width, geometry.height);
        auto og = get_output_geometry();
        log_info("ogeometry is %d@%d %dx%d", og.x, og.y, og.width, og.height);
    }

    void map(wlr_surface *surface)
    {
        log_info("map unmanaged %p", surface);
        wayfire_surface_t::map(surface);
        wayfire_view_t::move(xw->x, xw->y, false);
        damage();

        output->workspace->add_view_to_layer(self(), WF_LAYER_XWAYLAND);
    }

    void unmap()
    {
        wayfire_surface_t::unmap();
        output->workspace->add_view_to_layer(self(), 0);
    }

    void activate(bool active)
    {
        wayfire_view_t::activate(active);
        wlr_xwayland_surface_activate(xw, active);
    }


    void send_configure()
    {
        wlr_xwayland_surface_configure(xw, geometry.x, geometry.y,
                                       geometry.width, geometry.height);
        damage();
    }

    void move(int x, int y, bool s)
    {
        damage();
        geometry.x = x;
        geometry.y = y;
        send_configure();
    }

    void resize(int w, int h, bool s)
    {
        damage();
        geometry.width = w;
        geometry.height = h;
        send_configure();
    }

    /* TODO: bad with decoration */
    void set_geometry(wf_geometry g)
    {
        damage();
        geometry = g;
        send_configure();
    }

    void close()
    {
        wlr_xwayland_surface_close(xw);
    }

    void dec_keep_count()
    {
        wayfire_surface_t::dec_keep_count();
        log_info("dec keep count");
    }

    virtual void render_fb(int x, int y, pixman_region32_t* damage, int target_fb)
    {
        log_info("render fb unmanaged");
        wayfire_view_t::render_fb(x, y, damage, target_fb);
    }

    wlr_surface *get_keyboard_focus_surface()
    {
        if (wlr_xwayland_surface_is_unmanaged(xw))
            return nullptr;
        return surface;
    }

    ~wayfire_unmanaged_xwayland_view()
    {
        log_info("destroy unmanaged xwayland view");
    }

    std::string get_title()  { return nonull(xw->title);   }
    std::string get_app_id() { return nonull(xw->class_t); }
};


void notify_xwayland_created(wl_listener *, void *data)
{
    auto xsurf = (wlr_xwayland_surface*) data;

    wayfire_view view = nullptr;
    if (wlr_xwayland_surface_is_unmanaged(xsurf) || xsurf->override_redirect)
    {
        view = std::make_shared<wayfire_unmanaged_xwayland_view> (xsurf);
    } else
    {
        view = std::make_shared<wayfire_xwayland_view> (xsurf);
    }

    core->add_view(view);
    log_info("xwayland create %p -> %p", xsurf, view.get());
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

    log_info("xwayland display started at%d", core->api->xwayland->display);
    wl_signal_add(&core->api->xwayland->events.new_surface, &core->api->xwayland_created);
}


