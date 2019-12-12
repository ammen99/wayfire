#include <wayfire/util/log.hpp>
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
        wm_geometry.x = parent_g.x + (parent_g.width  - wm_geometry.width) / 2;
        wm_geometry.y = parent_g.y + (parent_g.height - wm_geometry.height) / 2;
    }
    else
    {
        /* if we have a parent which still isn't mapped, we cannot determine
         * the view's position, so we center it on the screen */
        wm_geometry.x = workarea.width / 2 - wm_geometry.width / 2;
        wm_geometry.y = workarea.height/ 2 - wm_geometry.height/ 2;
    }

    /* make sure view is visible afterwards */
    wm_geometry = clamp(wm_geometry, workarea);
    view->move(wm_geometry.x, wm_geometry.y);
    if (wm_geometry.width != view->get_wm_geometry().width ||
        wm_geometry.height != view->get_wm_geometry().height)
    {
        view->resize(wm_geometry.width, wm_geometry.height);
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
            new_parent->children.insert(new_parent->children.begin(), self());

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
        LOGE("in_continuous_resize counter dropped below 0!");
}

void wf::view_interface_t::set_moving(bool moving)
{
    auto& in_move = this->view_impl->in_continuous_move;

    in_move += moving ? 1 : -1;
    if (in_move < 0)
        LOGE("in_continuous_move counter dropped below 0!");
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

#define INVALID_COORDS(p) (std::isnan(p.x) || std::isnan(p.y))

wf_pointf wf::view_interface_t::global_to_local_point(const wf_pointf& arg,
    wf::surface_interface_t* surface)
{
    if (!is_mapped())
        return arg;

    /* First, untransform the coordinates to make them relative to the view's
     * internal coordinate system */
    wf_pointf result = arg;
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
    wf_pointf cursor, wf_pointf& local)
{
    if (!is_mapped())
        return nullptr;

    auto view_relative_coordinates =
        global_to_local_point(cursor, nullptr);

    for (auto& child : enumerate_surfaces({0, 0}))
    {
        local.x = view_relative_coordinates.x - child.position.x;
        local.y = view_relative_coordinates.y - child.position.y;

        if (child.surface->accepts_input(
                std::floor(local.x), std::floor(local.y)))
        {
            return child.surface;
        }
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
        get_output()->focus_view(self(), true);
    }

    desktop_state_updated();
}

void wf::view_interface_t::set_tiled(uint32_t edges)
{
    // store last unmaximized geometry for restoring
    if (edges && !this->tiled_edges && is_mapped())
        this->view_impl->last_windowed_geometry = get_wm_geometry();

    this->tiled_edges = edges;
    if (view_impl->frame)
        view_impl->frame->notify_view_tiled();

    this->emit_signal("tiled", nullptr);
    desktop_state_updated();
}

void wf::view_interface_t::set_fullscreen(bool full)
{
    /* When fullscreening a view, we want to store the last geometry it had
     * before getting fullscreen so that we can restore to it */
    if (full && !fullscreen)
    {
        if (this->tiled_edges) {
            this->view_impl->last_maximized_geometry = get_wm_geometry();
        } else {
            this->view_impl->last_windowed_geometry = get_wm_geometry();
        }
    }

    fullscreen = full;
    if (view_impl->frame)
        view_impl->frame->notify_view_fullscreen();

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

    this->emit_signal("fullscreen", nullptr);
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

void wf::view_interface_t::tile_request(uint32_t edges)
{
    if (fullscreen)
        return;

    view_tiled_signal data;
    data.view = self();
    data.edges = edges;
    data.desired_size = edges ? get_output()->workspace->get_workarea() :
        view_impl->last_windowed_geometry;

    set_tiled(edges);
    if (is_mapped())
        get_output()->emit_signal("view-maximized-request", &data);

    if (!data.carried_out)
    {
        if (data.desired_size.width > 0) {
            set_geometry(data.desired_size);
        } else {
            request_native_size();
        }
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
    auto wo = (out ?: (get_output() ?: wf::get_core().get_active_output()));
    assert(wo);

    /* TODO: what happens if the view is moved to the other output, but not
     * fullscreened? We should make sure that it stays visible there */
    if (get_output() != wo)
        wf::get_core().move_view_to_output(self(), wo);

    view_fullscreen_signal data;
    data.view = self();
    data.state = state;
    data.desired_size = get_output()->get_relative_geometry();

    if (!state)
    {
        data.desired_size = this->tiled_edges ?
            this->view_impl->last_maximized_geometry :
            this->view_impl->last_windowed_geometry;
    }

    set_fullscreen(state);
    if (is_mapped())
        wo->emit_signal("view-fullscreen-request", &data);

    if (!data.carried_out)
    {
        if (data.desired_size.width > 0) {
            set_geometry(data.desired_size);
        } else {
            request_native_size();
        }
    }
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
    if (!frame)
    {
        damage();
        if (view_impl->decoration)
            view_impl->decoration->unref();
        view_impl->decoration = nullptr;
        view_impl->frame = nullptr;
        emit_signal("decoration-changed", nullptr);

        return;
    }

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

    /* Calculate the wm geometry of the view after adding the decoration.
     *
     * If the view is neither maximized nor fullscreen, then we want to expand
     * the view geometry so that the actual view contents retain their size.
     *
     * For fullscreen and maximized views we want to "shrink" the view contents
     * so that the total wm geometry remains the same as before. */
    wf_geometry target_wm_geometry;
    if (!fullscreen && !this->tiled_edges)
    {
        target_wm_geometry = view_impl->frame->expand_wm_geometry(wm);
        // make sure that the view doesn't go outside of the screen or such
        auto wa = get_output()->workspace->get_workarea();
        auto visible = wf_geometry_intersection(target_wm_geometry, wa);
        if (visible != target_wm_geometry)
        {
            target_wm_geometry.x = wm.x;
            target_wm_geometry.y = wm.y;
        }
    } else if (fullscreen) {
        target_wm_geometry = get_output()->get_relative_geometry();
    } else if (this->tiled_edges) {
        target_wm_geometry = get_output()->workspace->get_workarea();
    }

    // notify the frame of the current size
    view_impl->frame->notify_view_resized(get_wm_geometry());
    // but request the target size, it will be sent to the frame on the
    // next commit
    set_geometry(target_wm_geometry);
    damage();

    emit_signal("decoration-changed", nullptr);
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

wf_pointf wf::view_interface_t::transform_point(const wf_pointf& point)
{
    auto result = point;
    auto view = get_untransformed_bounding_box();

    view_impl->transforms.for_each([&] (auto& tr)
    {
        result = tr->transform->local_to_transformed_point(view, result);
        view = tr->transform->get_bounding_box(view, view);
    });

    return result;
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
    if (!is_mapped() && !view_impl->offscreen_buffer.valid())
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

    /* This can happen in two ways:
     * 1. The view is unmapped, and no snapshot
     * 2. The last transform was deleted while iterating, so now the last
     *    transform is invalid in the list
     *
     * In both cases, we simply render whatever contents we have to the
     * framebuffer. */
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
    if (!get_output())
        return;

    auto damage_box = get_output()->render->get_target_framebuffer().
        damage_box_from_geometry_box(box);

    /* shell views are visible in all workspaces. That's why we must apply
     * their damage to all workspaces as well */
    if (role == wf::VIEW_ROLE_SHELL_VIEW)
    {
        auto wsize = get_output()->workspace->get_workspace_grid_size();
        auto cws = get_output()->workspace->get_current_workspace();

        /* Damage only the visible region of the shell view.
         * This prevents hidden panels from spilling damage onto other workspaces */
        wlr_box ws_box = get_output()->render->get_damage_box();
        wlr_box visible_damage = wf_geometry_intersection(damage_box, ws_box);

        for (int i = 0; i < wsize.width; i++)
        {
            for (int j = 0; j < wsize.height; j++)
            {
                const int dx = (i - cws.x) * ws_box.width;
                const int dy = (j - cws.y) * ws_box.height;
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
