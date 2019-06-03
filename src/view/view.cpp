#include "debug.hpp"
#include "../core/core-impl.hpp"
#include "view-impl.hpp"
#include "opengl.hpp"
#include "output.hpp"
#include "view.hpp"
#include "view-transform.hpp"
#include "decorator.hpp"
#include "workspace-manager.hpp"
#include "render-manager.hpp"
#include "xdg-shell.hpp"
#include "../output/gtk-shell.hpp"

#include <algorithm>
#include <glm/glm.hpp>
#include "signal-definitions.hpp"

extern "C"
{
#define static
#include <wlr/config.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/region.h>
#include <wlr/util/edges.h>
#undef static
}

static void reposition_relative_to_parent(wayfire_view view)
{
    if (!view->parent)
        return;

    auto workarea = view->get_output()->workspace->get_workarea();
    auto wm_geometry = view->get_wm_geometry();

    if (view->parent->is_mapped())
    {
        auto parent_g = view->parent->get_wm_geometry();
        int sx = parent_g.x + (parent_g.width  - wm_geometry.width) / 2;
        int sy = parent_g.y + (parent_g.height - wm_geometry.height) / 2;

        view->move(sx, sy);
    }
    else
    {
        /* if we have a parent which still isn't mapped, we cannot determine
         * the view's position, so we center it on the screen */
        int sx = workarea.width / 2 - wm_geometry.width / 2;
        int sy = workarea.height/ 2 - wm_geometry.height/ 2;

        view->move(sx, sy);
    }
}

static void unset_toplevel_parent(wayfire_view view)
{
    if (view->parent)
    {
        auto& container = view->parent->children;
        auto it = std::remove(container.begin(), container.end(), view);
        container.erase(it, container.end());
    }
}

void wf::view_interface_t::set_toplevel_parent(wayfire_view new_parent)
{
    if (parent != new_parent)
    {
        /* Erase from the old parent */
        unset_toplevel_parent(self());

        /* Add in the list of the new parent */
        if (new_parent)
            new_parent->children.push_back(self());

        parent = new_parent;
    }

    /* if the view isn't mapped, then it will be positioned properly in map() */
    if (is_mapped() && parent)
        reposition_relative_to_parent(self());
}

void wf::view_interface_t::set_role(view_role_t new_role)
{
    role = new_role;
    damage();
}

std::string wf::view_interface_t::to_string() const
{
    return "view-" + wf::object_base_t::to_string();
}

wayfire_view wf::view_interface_t::self()
{
    return wayfire_view(this);
}

/** Set the view's output. */
void wf::view_interface_t::set_output(wf::output_t* new_output)
{
    /* Make sure the view doesn't stay on the old output */
    if (get_output() && get_output() != new_output)
        get_output()->workspace->remove_view(self());

    _output_signal data;
    data.output = get_output();

    surface_interface_t::set_output(new_output);
    if (new_output != data.output)
        emit_signal("set-output", &data);
}

void wf::view_interface_t::resize(int w, int h)
{
    /* no-op */
}

void wf::view_interface_t::set_geometry(wf_geometry g)
{
    move(g.x, g.y);
    resize(g.width, g.height);
}

void wf::view_interface_t::set_resizing(bool resizing, uint32_t edges)
{
    /* edges are reset on the next commit */
    if (resizing)
        this->view_impl->edges = edges;

    auto& in_resize = this->view_impl->in_continuous_resize;
    in_resize += resizing ? 1 : -1;

    if (in_resize < 0)
        log_error("in_continuous_resize counter dropped below 0!");
}

void wf::view_interface_t::set_moving(bool moving)
{
    auto& in_move = this->view_impl->in_continuous_move;

    in_move += moving ? 1 : -1;
    if (in_move < 0)
        log_error("in_continuous_move counter dropped below 0!");
}

void wf::view_interface_t::request_native_size()
{
    /* no-op */
}

void wf::view_interface_t::close()
{
    /* no-op */
}

wf_geometry wf::view_interface_t::get_wm_geometry()
{
    return get_output_geometry();
}

wlr_box wf::view_interface_t::get_bounding_box()
{
    return transform_region(get_untransformed_bounding_box());
}

#define INVALID_COORDS(p) (p.x == WF_INVALID_INPUT_COORDINATES || \
    p.y == WF_INVALID_INPUT_COORDINATES)

wf_point wf::view_interface_t::global_to_local_point(const wf_point& arg,
    wf::surface_interface_t* surface)
{
    if (!is_mapped())
        return arg;

    /* First, untransform the coordinates to make them relative to the view's
     * internal coordinate system */
    wf_point result = arg;
    if (view_impl->transforms.size())
    {
        auto box = get_untransformed_bounding_box();
        view_impl->transforms.for_each_reverse([&] (auto& tr)
        {
            if (INVALID_COORDS(result))
                return;

            auto& transform = tr->transform;
            result = transform->transformed_to_local_point(box, result);
            box = transform->get_bounding_box(box, box);
        });

        if (INVALID_COORDS(result))
            return result;
    }

    /* Make cooordinates relative to the view */
    auto og = get_output_geometry();
    result.x -= og.x;
    result.y -= og.y;

    /* Go up from the surface, finding offsets */
    while (surface && surface != this)
    {
        auto offset = surface->get_offset();
        result.x -= offset.x;
        result.y -= offset.y;

        surface = surface->priv->parent_surface;
    }

    return result;
}

wf::surface_interface_t *wf::view_interface_t::map_input_coordinates(
    int cursor_x, int cursor_y, int &sx, int &sy)
{
    if (!is_mapped())
        return nullptr;

    auto view_relative_coordinates =
        global_to_local_point({cursor_x, cursor_y}, nullptr);

    for (auto& child : enumerate_surfaces({0, 0}))
    {
        sx = view_relative_coordinates.x - child.position.x;
        sy = view_relative_coordinates.y - child.position.y;

        if (child.surface->accepts_input(sx, sy))
            return child.surface;
    }

    return nullptr;
}

bool wf::view_interface_t::is_focuseable() const
{
    return view_impl->keyboard_focus_enabled;
}

void wf::view_interface_t::set_minimized(bool minim)
{
    minimized = minim;
    if (minimized)
    {
        view_disappeared_signal data;
        data.view = self();
        emit_signal("disappeared", &data);

        get_output()->emit_signal("view-disappeared", &data);
        get_output()->workspace->add_view(self(), wf::LAYER_MINIMIZED);

        /* We want to be sure that when we restore the view, it will be visible
         * on the then current workspace
         *
         * Because the minimized layer doesn't move when switching workspaces,
         * we know that making it "visible" in the minimize layer will ensure
         * it is visible when we restore it */
        get_output()->workspace->move_to_workspace(self(),
            get_output()->workspace->get_current_workspace());
    } else
    {
        get_output()->workspace->add_view(self(), wf::LAYER_WORKSPACE);
        get_output()->focus_view(self());
    }

    desktop_state_updated();
}

void wf::view_interface_t::set_tiled(uint32_t edges)
{
    /* Most shells don't support the tiled state.
     * In this case, just default to maximize */
    if (edges) {
        set_maximized(true);
    } else {
        set_maximized(false);
    }

    tiled_edges = edges;
}

void wf::view_interface_t::set_maximized(bool maxim)
{
    maximized = maxim;

    if (view_impl->frame)
        view_impl->frame->notify_view_maximized();

    desktop_state_updated();
}

void wf::view_interface_t::set_fullscreen(bool full)
{
    fullscreen = full;

    if (view_impl->frame)
        view_impl->frame->notify_view_fullscreened();

    if (fullscreen && get_output())
    {
        /* Will trigger raising to fullscreen layer in workspace-manager */
        get_output()->workspace->bring_to_front(self());
    }

    /**
     * If the view is no longer fullscreen, we potentially need to lower it
     * to the workspace layer
     */
    bool needs_lowering =
        get_output()->workspace->get_view_layer(self()) == wf::LAYER_FULLSCREEN;

    if (!fullscreen && get_output() && needs_lowering)
        get_output()->workspace->add_view(self(), wf::LAYER_WORKSPACE);

    desktop_state_updated();
}

void wf::view_interface_t::set_activated(bool active)
{
    if (view_impl->frame)
        view_impl->frame->notify_view_activated(active);

    activated = active;
    desktop_state_updated();
}

void wf::view_interface_t::desktop_state_updated()
{
    /* no-op */
}

void wf::view_interface_t::move_request()
{
    move_request_signal data;
    data.view = self();
    get_output()->emit_signal("move-request", &data);
}

void wf::view_interface_t::focus_request()
{
    wf::get_core().focus_view(self());
    get_output()->ensure_visible(self());
}

void wf::view_interface_t::resize_request(uint32_t edges)
{
    resize_request_signal data;
    data.view = self();
    data.edges = edges;
    get_output()->emit_signal("resize-request", &data);
}

void wf::view_interface_t::maximize_request(bool state)
{
    if (maximized == state)
        return;

    view_maximized_signal data;
    data.view = self();
    data.state = state;

    if (is_mapped())
    {
        get_output()->emit_signal("view-maximized-request", &data);
    }
    else if (state)
    {
        set_geometry(get_output()->workspace->get_workarea());
        set_maximized(state);
        set_tiled(WLR_EDGE_TOP | WLR_EDGE_LEFT
            | WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT);
        get_output()->emit_signal("view-maximized", &data);
    }
}

void wf::view_interface_t::minimize_request(bool state)
{
    if (state == minimized || !is_mapped())
        return;

    view_minimize_request_signal data;
    data.view = self();
    data.state = state;

    if (is_mapped())
    {
        get_output()->emit_signal("view-minimize-request", &data);
        /* Some plugin (e.g animate) will take care of the request, so we need
         * to just send proper state to foreign-toplevel clients */
        if (data.carried_out)
        {
            minimized = state;
            desktop_state_updated();
            get_output()->refocus(self());
        } else
        {
            /* Do the default minimization */
            set_minimized(state);
        }
    }
}

void wf::view_interface_t::fullscreen_request(wf::output_t *out, bool state)
{
    if (fullscreen == state)
        return;

    auto wo = (out ?: (get_output() ?: wf::get_core().get_active_output()));
    assert(wo);

    /* TODO: what happens if the view is moved to the other output, but not
     * fullscreened? We should make sure that it stays visible there */
    if (get_output() != wo)
        wf::get_core().move_view_to_output(self(), wo);

    view_fullscreen_signal data;
    data.view = self();
    data.state = state;

    if (is_mapped())
    {
        wo->emit_signal("view-fullscreen-request", &data);
    } else if (state)
    {
        set_geometry(get_output()->get_relative_geometry());
        get_output()->emit_signal("view-fullscreen", &data);
    }

    set_fullscreen(state);
}

bool wf::view_interface_t::is_visible()
{
    if (is_mapped())
        return true;

    /* If we have an unmapped view, then there are two cases:
     *
     * 1. View has been "destroyed". In this case, the view is visible as long
     * as it has at least one reference (for ex. a plugin which shows unmap
     * animation)
     *
     * 2. View hasn't been "destroyed", just unmapped. Here we need to have at
     * least 2 references, which would mean that the view is in unmap animation.
     */
    if (view_impl->is_alive) {
        return priv->ref_cnt >= 2;
    } else {
        return priv->ref_cnt >= 1;
    }
}

void wf::view_interface_t::damage()
{
    damage_box(get_untransformed_bounding_box());
}

wlr_box wf::view_interface_t::get_minimize_hint()
{
    return { 0, 0, 0, 0 };
}

bool wf::view_interface_t::should_be_decorated()
{
    return false;
}

void wf::view_interface_t::set_decoration(surface_interface_t *frame)
{
    assert(frame->priv->parent_surface == this);

    // Take wm geometry as it was before adding the frame */
    auto wm = get_wm_geometry();

    /* First, delete old decoration if any */
    damage();
    if (view_impl->decoration)
        view_impl->decoration->unref();

    view_impl->decoration = frame;
    view_impl->frame = dynamic_cast<wf_decorator_frame_t*> (frame);
    assert(frame);

    /* Move the decoration as the last child surface */
    auto container = this->priv->surface_children;
    auto it = std::remove(container.begin(), container.end(), frame);
    container.erase(it);
    container.push_back(frame);

    /* Adjust view geometry, but retain view position */
    view_impl->frame->notify_view_resized(
        view_impl->frame->expand_wm_geometry(wm));
    move(wm.x, wm.y);

    damage();

    if (maximized)
    {
        set_geometry(get_output()->workspace->get_workarea());
        view_impl->frame->notify_view_maximized();
    }

    if (fullscreen)
    {
        set_geometry(get_output()->get_relative_geometry());
        view_impl->frame->notify_view_fullscreened();
    }
}

void wf::view_interface_t::add_transformer(
    std::unique_ptr<wf_view_transformer_t> transformer)
{
    add_transformer(std::move(transformer), "");
}

void wf::view_interface_t::add_transformer(
    std::unique_ptr<wf_view_transformer_t> transformer, std::string name)
{
    damage();

    auto tr = std::make_shared<wf::view_transform_block_t> ();
    tr->transform = std::move(transformer);
    tr->plugin_name = name;

    view_impl->transforms.emplace_at(std::move(tr), [&] (auto& other)
    {
        if (other->transform->get_z_order() >= tr->transform->get_z_order())
            return view_impl->transforms.INSERT_BEFORE;
        return view_impl->transforms.INSERT_NONE;
    });

    damage();
}

nonstd::observer_ptr<wf_view_transformer_t>
wf::view_interface_t::get_transformer(
    std::string name)
{
    nonstd::observer_ptr<wf_view_transformer_t> result{nullptr};
    view_impl->transforms.for_each([&] (auto& tr) {
        if (tr->plugin_name == name)
            result = nonstd::make_observer(tr->transform.get());
    });

    return result;
}

void wf::view_interface_t::pop_transformer(
    nonstd::observer_ptr<wf_view_transformer_t> transformer)
{
    view_impl->transforms.remove_if([&] (auto& tr)
    {
        return tr->transform.get() == transformer.get();
    });

    /* Since we can remove transformers while rendering the output, damaging it
     * won't help at this stage (damage is already calculated).
     *
     * Instead, we directly damage the whole output for the next frame */
    get_output()->render->damage_whole_idle();
}

void wf::view_interface_t::pop_transformer(std::string name)
{
    pop_transformer(get_transformer(name));
}

bool wf::view_interface_t::has_transformer()
{
    return view_impl->transforms.size();
}

wf_geometry wf::view_interface_t::get_untransformed_bounding_box()
{
    if (!is_mapped())
        return view_impl->offscreen_buffer.geometry;

    auto bbox = get_output_geometry();
    wf_region bounding_region = bbox;

    for (auto& child : enumerate_surfaces({bbox.x, bbox.y}))
    {
        auto dim = child.surface->get_size();
        bounding_region |= {child.position.x, child.position.y,
            dim.width, dim.height};
    };

    return wlr_box_from_pixman_box(bounding_region.get_extents());
}

wlr_box wf::view_interface_t::get_bounding_box(std::string transformer)
{
    return get_bounding_box(get_transformer(transformer));
}

wlr_box wf::view_interface_t::get_bounding_box(
    nonstd::observer_ptr<wf_view_transformer_t> transformer)
{
    return transform_region(get_untransformed_bounding_box(), transformer);
}

wlr_box wf::view_interface_t::transform_region(const wlr_box& region,
    nonstd::observer_ptr<wf_view_transformer_t> upto)
{
    auto box = region;
    auto view = get_untransformed_bounding_box();

    bool computed_region = false;
    view_impl->transforms.for_each([&] (auto& tr)
    {
        if (computed_region || tr->transform.get() == upto.get())
        {
            computed_region = true;
            return;
        }

        box = tr->transform->get_bounding_box(view, box);
        view = tr->transform->get_bounding_box(view, view);
    });

    return box;
}

wlr_box wf::view_interface_t::transform_region(const wlr_box& region,
    std::string transformer)
{
    return transform_region(region, get_transformer(transformer));
}

wlr_box wf::view_interface_t::transform_region(const wlr_box& region)
{
    return transform_region(region,
        nonstd::observer_ptr<wf_view_transformer_t>(nullptr));
}

bool wf::view_interface_t::intersects_region(const wlr_box& region)
{
    /* fallback to the whole transformed boundingbox, if it exists */
    if (!is_mapped())
        return region & get_bounding_box();

    auto origin = get_output_geometry();
    for (auto& child : enumerate_surfaces({origin.x, origin.y}))
    {
        wlr_box box = {child.position.x, child.position.y,
            child.surface->get_size().width, child.surface->get_size().height};
        box = transform_region(box);

        if (region & box)
            return true;
    }

    return false;
}

bool wf::view_interface_t::render_transformed(const wf_framebuffer& framebuffer,
    const wf_region& damage)
{
    if (!has_transformer())
        return false;

    take_snapshot();
    auto& offscreen_buffer = view_impl->offscreen_buffer;
    auto& transforms = view_impl->transforms;

    /* Render the view passing its snapshot through the transformers.
     * For each transformer except the last we render on offscreen buffers,
     * and the last one is rendered to the real fb. */
    wf_geometry obox = get_untransformed_bounding_box();
    obox.width = offscreen_buffer.geometry.width;
    obox.height = offscreen_buffer.geometry.height;

    /* We keep a shared_ptr to the previous transform which we executed, so that
     * even if it gets removed, its texture remains valid.
     *
     * NB: we do not call previous_transform's transformer functions after the
     * cycle is complete, because the memory might have already been freed.
     * We only know that the texture is still alive. */
    std::shared_ptr<view_transform_block_t> previous_transform = nullptr;
    GLuint previous_texture = offscreen_buffer.tex;

    /* final_transform is the one that should render to the screen */
    std::shared_ptr<view_transform_block_t> final_transform = nullptr;

    transforms.for_each([&] (auto& transform) -> void
    {
        /* Last transform is handled separately */
        if (transform == transforms.back())
        {
            final_transform = transform;
            return;
        }

        /* Calculate size after this transform */
        auto transformed_box =
            transform->transform->get_bounding_box(obox, obox);

        /* Prepare buffer to store result after the transform */
        OpenGL::render_begin();
        transform->fb.allocate(transformed_box.width, transformed_box.height);
        transform->fb.geometry = transformed_box;
        transform->fb.bind(); // bind buffer to clear it
        OpenGL::clear({0, 0, 0, 0});
        OpenGL::render_end();

        /* Actually render the transform to the next framebuffer */
        wf_region whole_region{wlr_box{0, 0,
            transformed_box.width, transformed_box.height}};
        transform->transform->render_with_damage(previous_texture, obox,
            whole_region, transform->fb);

        previous_transform = transform;
        previous_texture = previous_transform->fb.tex;
        obox = transformed_box;
    });

    /* Pretty "exotic" case: the last transformer was deleted before we could
     * use it. In this case, just manually display the view */
    if (final_transform == nullptr)
    {
        OpenGL::render_begin(framebuffer);
        auto matrix = framebuffer.get_orthographic_projection();
        gl_geometry src_geometry = {
            1.0f * obox.x, 1.0f * obox.y,
            1.0f * obox.x + 1.0f * obox.width,
            1.0f * obox.y + 1.0f * obox.height,
        };

        for (const auto& rect : damage)
        {
            framebuffer.scissor(wlr_box_from_pixman_box(rect));
            OpenGL::render_transformed_texture(previous_texture, src_geometry,
                {}, matrix);
        }

        OpenGL::render_end();
    } else
    {
        /* Regular case, just call the last transformer, but render directly
         * to the target framebuffer */
        final_transform->transform->render_with_damage(previous_texture, obox,
            damage, framebuffer);
    }

    return true;
}

wf::view_transform_block_t::view_transform_block_t() {}
wf::view_transform_block_t::~view_transform_block_t()
{
    OpenGL::render_begin();
    this->fb.release();
    OpenGL::render_end();
}

void wf::view_interface_t::take_snapshot()
{
    if (!is_mapped())
        return;

    auto& offscreen_buffer = view_impl->offscreen_buffer;

    auto buffer_geometry = get_untransformed_bounding_box();
    offscreen_buffer.geometry = buffer_geometry;

    float scale = get_output()->handle->scale;

    /* Nothing has changed, the last buffer is still valid */
    if (offscreen_buffer.cached_damage.empty())
        return;

    /* TODO: use offscreen buffer better */
    offscreen_buffer.cached_damage.clear();

    OpenGL::render_begin();
    offscreen_buffer.allocate(buffer_geometry.width * scale,
        buffer_geometry.height * scale);

    offscreen_buffer.scale = scale;
    offscreen_buffer.bind();
    OpenGL::clear({0, 0, 0, 0});
    OpenGL::render_end();

    wf_region full_region{{0, 0, offscreen_buffer.viewport_width,
        offscreen_buffer.viewport_height}};

    auto output_geometry = get_output_geometry();
    int ox = output_geometry.x - buffer_geometry.x;
    int oy = output_geometry.y - buffer_geometry.y;

    auto children = enumerate_surfaces({ox, oy});
    for (auto& child : wf::reverse(children))
    {
        child.surface->simple_render(offscreen_buffer,
            child.position.x, child.position.y, full_region);
    }
}

wf::view_interface_t::view_interface_t() : surface_interface_t(nullptr)
{
    this->view_impl = std::make_unique<wf::view_interface_t::view_priv_impl>();
    set_output(wf::get_core().get_active_output());
}

wf::view_interface_t::~view_interface_t()
{
    /* Note: at this point, it is invalid to call most functions */
    unset_toplevel_parent(self());
}

void wf::view_interface_t::damage_box(const wlr_box& box)
{
    if (!get_output())
        return;

    view_impl->offscreen_buffer.cached_damage |= box;
    damage_raw(transform_region(box));
}

void wf::view_interface_t::damage_raw(const wlr_box& box)
{
    auto damage_box = get_output()->render->get_target_framebuffer().
        damage_box_from_geometry_box(box);

    /* shell views are visible in all workspaces. That's why we must apply
     * their damage to all workspaces as well */
    if (role == wf::VIEW_ROLE_SHELL_VIEW)
    {
        GetTuple(vw, vh, get_output()->workspace->get_workspace_grid_size());
        GetTuple(vx, vy, get_output()->workspace->get_current_workspace());

        /* Damage only the visible region of the shell view.
         * This prevents hidden panels from spilling damage onto other workspaces */
        wlr_box ws_box = get_output()->render->get_damage_box();
        wlr_box visible_damage = wf_geometry_intersection(damage_box, ws_box);

        for (int i = 0; i < vw; i++)
        {
            for (int j = 0; j < vh; j++)
            {
                const int dx = (i - vx) * ws_box.width;
                const int dy = (j - vy) * ws_box.height;
                get_output()->render->damage(visible_damage + wf_point{dx, dy});
            }
        }
    } else
    {
        get_output()->render->damage(damage_box);
    }

    emit_signal("damaged-region", nullptr);
}

void wf::view_interface_t::destruct()
{
    view_impl->is_alive = false;
    view_impl->idle_destruct.run_once(
        [&] () {wf::get_core_impl().erase_view(self());});
}
