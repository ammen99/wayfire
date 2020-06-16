#include "output-impl.hpp"
#include "wayfire/view.hpp"
#include "../core/core-impl.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/render-manager.hpp"
#include "wayfire/output-layout.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wayfire/compositor-view.hpp"
#include "wayfire-shell.hpp"
#include "../core/seat/input-manager.hpp"
#include "../view/xdg-shell.hpp"
#include <wayfire/util/log.hpp>

#include <linux/input.h>
extern "C"
{
#include <wlr/types/wlr_output.h>
}

#include <algorithm>
#include <assert.h>

wf::output_t::output_t() = default;

wf::output_impl_t::output_impl_t(wlr_output *handle,
    const wf::dimensions_t& effective_size)
{
    this->set_effective_size(effective_size);
    this->handle = handle;
    workspace    = std::make_unique<workspace_manager>(this);
    render = std::make_unique<render_manager>(this);

    view_disappeared_cb = [=] (wf::signal_data_t *data)
    {
        output_t::refocus(get_signaled_view(data));
    };

    connect_signal("view-disappeared", &view_disappeared_cb);
}

void wf::output_impl_t::start_plugins()
{
    plugin = std::make_unique<plugin_manager>(this);
}

std::string wf::output_t::to_string() const
{
    return handle->name;
}

void wf::output_impl_t::refocus(wayfire_view skip_view, uint32_t layers)
{
    wayfire_view next_focus = nullptr;
    auto views = workspace->get_views_on_workspace(
        workspace->get_current_workspace(), layers);

    for (auto v : views)
    {
        if ((v != skip_view) && v->is_mapped() &&
            v->get_keyboard_focus_surface())
        {
            next_focus = v;
            break;
        }
    }

    focus_view(next_focus, 0u);
}

void wf::output_t::refocus(wayfire_view skip_view)
{
    uint32_t focused_layer = wf::get_core().get_focused_layer();
    uint32_t layers = focused_layer <= LAYER_WORKSPACE ? WM_LAYERS : focused_layer;

    auto views = workspace->get_views_on_workspace(
        workspace->get_current_workspace(), layers);

    if (views.empty())
    {
        if (wf::get_core().get_active_output() == this)
        {
            LOGD("warning: no focused views in the focused layer, probably a bug");
        }

        /* Usually, we focus a layer so that a particular view has focus, i.e
         * we expect that there is a view in the focused layer. However we
         * should try to find reasonable focus in any focuseable layers if
         * that is not the case, for ex. if there is a focused layer by a
         * layer surface on another output */
        layers = all_layers_not_below(focused_layer);
    }

    refocus(skip_view, layers);
}

wf::output_t::~output_t()
{
    wf::get_core_impl().input->free_output_bindings(this);
}

wf::output_impl_t::~output_impl_t()
{}

void wf::output_impl_t::set_effective_size(const wf::dimensions_t& size)
{
    this->effective_size = size;
}

wf::dimensions_t wf::output_impl_t::get_screen_size() const
{
    return this->effective_size;
}

wf::geometry_t wf::output_t::get_relative_geometry() const
{
    auto size = get_screen_size();

    return {
        0, 0, size.width, size.height
    };
}

wf::geometry_t wf::output_t::get_layout_geometry() const
{
    auto box = wlr_output_layout_get_box(
        wf::get_core().output_layout->get_handle(), handle);
    if (box)
    {
        return *box;
    } else
    {
        LOGE("Get layout geometry for an invalid output!");

        return {0, 0, 1, 1};
    }
}

void wf::output_t::ensure_pointer(bool center) const
{
    auto ptr = wf::get_core().get_cursor_position();
    if (!center &&
        (get_layout_geometry() & wf::point_t{(int)ptr.x, (int)ptr.y}))
    {
        return;
    }

    auto lg = get_layout_geometry();
    wf::pointf_t target = {
        lg.x + lg.width / 2.0,
        lg.y + lg.height / 2.0,
    };
    wf::get_core().warp_cursor(target);
    wf::get_core().set_cursor("default");
}

wf::pointf_t wf::output_t::get_cursor_position() const
{
    auto og = get_layout_geometry();
    auto gc = wf::get_core().get_cursor_position();

    return {gc.x - og.x, gc.y - og.y};
}

bool wf::output_t::ensure_visible(wayfire_view v)
{
    auto bbox = v->get_bounding_box();
    auto g    = this->get_relative_geometry();

    /* Compute the percentage of the view which is visible */
    auto intersection = wf::geometry_intersection(bbox, g);
    double area = 1.0 * intersection.width * intersection.height;
    area /= 1.0 * bbox.width * bbox.height;

    if (area >= 0.1) /* View is somewhat visible, no need for anything special */
    {
        return false;
    }

    /* Otherwise, switch the workspace so the view gets maximum exposure */
    int dx = bbox.x + bbox.width / 2;
    int dy = bbox.y + bbox.height / 2;

    int dvx  = std::floor(1.0 * dx / g.width);
    int dvy  = std::floor(1.0 * dy / g.height);
    auto cws = workspace->get_current_workspace();
    workspace->request_workspace(cws + wf::point_t{dvx, dvy});

    return true;
}

template<class popup_type>
void try_close_popup(wayfire_view to_check, wayfire_view active_view)
{
    auto popup = dynamic_cast<wayfire_xdg_popup<popup_type>*>(to_check.get());
    if (!popup || (popup->popup_parent == active_view.get()))
    {
        return;
    }

    /* Ignore popups which have a popup as their parent. In those cases, we'll
     * close the topmost popup and this will recursively destroy the others.
     *
     * Otherwise we get a race condition with wlroots. */
    if (dynamic_cast<wayfire_xdg_popup<popup_type>*>(popup->popup_parent))
    {
        return;
    }

    popup->close();
}

void wf::output_impl_t::close_popups()
{
    for (auto& v : workspace->get_views_in_layer(wf::ALL_LAYERS))
    {
        try_close_popup<wlr_xdg_popup>(v, active_view);
        try_close_popup<wlr_xdg_popup_v6>(v, active_view);
    }
}

void wf::output_impl_t::update_active_view(wayfire_view v, uint32_t flags)
{
    this->active_view = v;
    if (this == wf::get_core().get_active_output())
    {
        wf::get_core().set_active_view(v);
    }

    if (flags & FOCUS_VIEW_CLOSE_POPUPS)
    {
        close_popups();
    }
}

void wf::output_impl_t::focus_view(wayfire_view v, uint32_t flags)
{
    const auto& make_view_visible = [this, flags] (wayfire_view view)
    {
        if (view->minimized)
        {
            view->minimize_request(false);
        }

        if (flags & FOCUS_VIEW_RAISE)
        {
            workspace->bring_to_front(view);
        }
    };

    if (v && (workspace->get_view_layer(v) < wf::get_core().get_focused_layer()))
    {
        auto active_view = get_active_view();
        if (active_view && (active_view->get_app_id().find("$unfocus") == 0))
        {
            /* This is the case where for ex. a panel has grabbed input focus,
             * but user has clicked on another view so we want to dismiss the
             * grab. We can't do that straight away because the client still
             * holds the focus layer request.
             *
             * Instead, we want to deactive the $unfocus view, so that it can
             * release the grab. At the same time, we bring the to-be-focused
             * view on top, so that it gets the focus next. */
            update_active_view(nullptr, flags);
            make_view_visible(v);
        } else
        {
            LOGD("Denying focus request for a view from a lower layer than the"
                 " focused layer");
        }

        return;
    }

    focus_view_signal data;

    if (!v || !v->is_mapped())
    {
        update_active_view(nullptr, flags);
        data.view = nullptr;
        emit_signal("focus-view", &data);
        return;
    }

    while (v->parent && v->parent->is_mapped())
    {
        v = v->parent;
    }

    /* If no keyboard focus surface is set, then we don't want to focus the view */
    if (v->get_keyboard_focus_surface() || interactive_view_from_view(v.get()))
    {
        make_view_visible(v);
        update_active_view(v, flags);
        data.view = v;
        emit_signal("view-focused", &data);
    }
}

void wf::output_impl_t::focus_view(wayfire_view v, bool raise)
{
    uint32_t flags = FOCUS_VIEW_CLOSE_POPUPS;
    if (raise)
    {
        flags |= FOCUS_VIEW_RAISE;
    }

    focus_view(v, flags);
}

wayfire_view wf::output_t::get_top_view() const
{
    auto views = workspace->get_views_on_workspace(
        workspace->get_current_workspace(),
        LAYER_WORKSPACE);

    return views.empty() ? nullptr : views[0];
}

wayfire_view wf::output_impl_t::get_active_view() const
{
    return active_view;
}

bool wf::output_impl_t::can_activate_plugin(const plugin_grab_interface_uptr& owner,
    uint32_t flags)
{
    if (!owner)
    {
        return false;
    }

    if (this->inhibited && !(flags & wf::PLUGIN_ACTIVATION_IGNORE_INHIBIT))
    {
        return false;
    }

    if (active_plugins.find(owner.get()) != active_plugins.end())
    {
        return flags & wf::PLUGIN_ACTIVATE_ALLOW_MULTIPLE;
    }

    for (auto act_owner : active_plugins)
    {
        bool compatible =
            ((act_owner->capabilities & owner->capabilities) == 0);
        if (!compatible)
        {
            return false;
        }
    }

    return true;
}

bool wf::output_impl_t::activate_plugin(const plugin_grab_interface_uptr& owner,
    uint32_t flags)
{
    if (!can_activate_plugin(owner, flags))
    {
        return false;
    }

    if (active_plugins.find(owner.get()) != active_plugins.end())
    {
        LOGD("output ", handle->name,
            ": activate plugin ", owner->name, " again");
    } else
    {
        LOGD("output ", handle->name, ": activate plugin ", owner->name);
    }

    active_plugins.insert(owner.get());

    return true;
}

bool wf::output_impl_t::deactivate_plugin(
    const plugin_grab_interface_uptr& owner)
{
    auto it = active_plugins.find(owner.get());
    if (it == active_plugins.end())
    {
        return true;
    }

    active_plugins.erase(it);
    LOGD("output ", handle->name, ": deactivate plugin ", owner->name);

    if (active_plugins.count(owner.get()) == 0)
    {
        owner->ungrab();
        active_plugins.erase(owner.get());

        return true;
    }

    return false;
}

bool wf::output_impl_t::is_plugin_active(std::string name) const
{
    for (auto act : active_plugins)
    {
        if (act && (act->name == name))
        {
            return true;
        }
    }

    return false;
}

wf::plugin_grab_interface_t*wf::output_impl_t::get_input_grab_interface()
{
    for (auto p : active_plugins)
    {
        if (p && p->is_grabbed())
        {
            return p;
        }
    }

    return nullptr;
}

void wf::output_impl_t::inhibit_plugins()
{
    this->inhibited = true;

    std::vector<wf::plugin_grab_interface_t*> ifaces;
    for (auto p : active_plugins)
    {
        if (p->callbacks.cancel)
        {
            ifaces.push_back(p);
        }
    }

    for (auto p : ifaces)
    {
        p->callbacks.cancel();
    }
}

void wf::output_impl_t::uninhibit_plugins()
{
    this->inhibited = false;
}

bool wf::output_impl_t::is_inhibited() const
{
    return this->inhibited;
}

/* simple wrappers for wf::get_core_impl().input, as it isn't exposed to plugins */
wf::binding_t*wf::output_t::add_key(option_sptr_t<keybinding_t> key,
    wf::key_callback *callback)
{
    return wf::get_core_impl().input->new_binding(WF_BINDING_KEY, key, this,
        callback);
}

wf::binding_t*wf::output_t::add_axis(option_sptr_t<keybinding_t> axis,
    wf::axis_callback *callback)
{
    return wf::get_core_impl().input->new_binding(WF_BINDING_AXIS, axis, this,
        callback);
}

wf::binding_t*wf::output_t::add_touch(option_sptr_t<keybinding_t> mod,
    wf::touch_callback *callback)
{
    return wf::get_core_impl().input->new_binding(WF_BINDING_TOUCH, mod, this,
        callback);
}

wf::binding_t*wf::output_t::add_button(option_sptr_t<buttonbinding_t> button,
    wf::button_callback *callback)
{
    return wf::get_core_impl().input->new_binding(WF_BINDING_BUTTON, button,
        this, callback);
}

wf::binding_t*wf::output_t::add_gesture(option_sptr_t<touchgesture_t> gesture,
    wf::gesture_callback *callback)
{
    return wf::get_core_impl().input->new_binding(WF_BINDING_GESTURE, gesture,
        this, callback);
}

wf::binding_t*wf::output_t::add_activator(
    option_sptr_t<activatorbinding_t> activator, wf::activator_callback *callback)
{
    return wf::get_core_impl().input->new_binding(WF_BINDING_ACTIVATOR, activator,
        this, callback);
}

void wf::output_t::rem_binding(wf::binding_t *binding)
{
    wf::get_core_impl().input->rem_binding(binding);
}

void wf::output_t::rem_binding(void *callback)
{
    wf::get_core_impl().input->rem_binding(callback);
}

namespace wf
{
uint32_t all_layers_not_below(uint32_t layer)
{
    uint32_t mask = 0;
    for (int i = 0; i < wf::TOTAL_LAYERS; i++)
    {
        if ((1u << i) >= layer)
        {
            mask |= (1 << i);
        }
    }

    return mask;
}
}
