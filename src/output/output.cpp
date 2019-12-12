#include "debug.hpp"
#include "output-impl.hpp"
#include "view.hpp"
#include "../core/core-impl.hpp"
#include "signal-definitions.hpp"
#include "render-manager.hpp"
#include "output-layout.hpp"
#include "workspace-manager.hpp"
#include "compositor-view.hpp"
#include "wayfire-shell.hpp"
#include "../core/seat/input-manager.hpp"
#include <wayfire/util/log.hpp>

#include <linux/input.h>
extern "C"
{
#include <wlr/types/wlr_output.h>
}

#include <algorithm>
#include <assert.h>

wf::output_t::output_t(wlr_output *handle)
{
    this->handle = handle;
    workspace = std::make_unique<workspace_manager> (this);
    render = std::make_unique<render_manager> (this);
}

wf::output_impl_t::output_impl_t(wlr_output *handle)
    : output_t(handle)
{
    plugin = std::make_unique<plugin_manager> (this);
    view_disappeared_cb = [=] (wf::signal_data_t *data) {
        output_t::refocus(get_signaled_view(data));
    };

    connect_signal("view-disappeared", &view_disappeared_cb);
    connect_signal("detach-view", &view_disappeared_cb);
}

std::string wf::output_t::to_string() const
{
    return handle->name;
}

void wf::output_impl_t::refocus(wayfire_view skip_view, uint32_t layers)
{
    wayfire_view next_focus = nullptr;
    auto views = workspace->get_views_on_workspace(workspace->get_current_workspace(), layers, true);

    for (auto v : views)
    {
        if (v != skip_view && v->is_mapped() &&
            v->get_keyboard_focus_surface())
        {
            next_focus = v;
            break;
        }
    }

    focus_view(next_focus, false);
}

void wf::output_t::refocus(wayfire_view skip_view)
{
    uint32_t focused_layer = wf::get_core().get_focused_layer();
    uint32_t layers = focused_layer <= LAYER_WORKSPACE ?  WM_LAYERS : focused_layer;

    auto views = workspace->get_views_on_workspace(
        workspace->get_current_workspace(), layers, true);

    if (views.empty())
    {
        if (wf::get_core().get_active_output() == this) {
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
wf::output_impl_t::~output_impl_t() { }

wf_size_t wf::output_t::get_screen_size() const
{
    int w, h;
    wlr_output_effective_resolution(handle, &w, &h);
    return {w, h};
}

wf_geometry wf::output_t::get_relative_geometry() const
{
    wf_geometry g;
    g.x = g.y = 0;
    wlr_output_effective_resolution(handle, &g.width, &g.height);

    return g;
}

wf_geometry wf::output_t::get_layout_geometry() const
{
    auto box = wlr_output_layout_get_box(
        wf::get_core().output_layout->get_handle(), handle);
    if (box) {
        return *box;
    } else {
        LOGE("Get layout geometry for an invalid output!");
        return {0, 0, 1, 1};
    }
}

/* TODO: is this still relevant? */
void wf::output_t::ensure_pointer() const
{
    /*
    auto ptr = weston_seat_get_pointer(wf::get_core().get_current_seat());
    if (!ptr) return;

    int px = wl_fixed_to_int(ptr->x), py = wl_fixed_to_int(ptr->y);

    auto g = get_layout_geometry();
    if (!point_inside({px, py}, g)) {
        wl_fixed_t cx = wl_fixed_from_int(g.x + g.width / 2);
        wl_fixed_t cy = wl_fixed_from_int(g.y + g.height / 2);

        weston_pointer_motion_event ev;
        ev.mask |= WESTON_POINTER_MOTION_ABS;
        ev.x = wl_fixed_to_double(cx);
        ev.y = wl_fixed_to_double(cy);

        weston_pointer_move(ptr, &ev);
    } */
}

wf_pointf wf::output_t::get_cursor_position() const
{
    auto og = get_layout_geometry();
    auto gc = wf::get_core().get_cursor_position();
    return {gc.x - og.x, gc.y - og.y};
}

bool wf::output_t::ensure_visible(wayfire_view v)
{
    auto bbox = v->get_bounding_box();
    auto g = this->get_relative_geometry();

    /* Compute the percentage of the view which is visible */
    auto intersection = wf_geometry_intersection(bbox, g);
    double area = 1.0 * intersection.width * intersection.height;
    area /= 1.0 * bbox.width * bbox.height;

    if (area >= 0.1) /* View is somewhat visible, no need for anything special */
        return false;

    /* Otherwise, switch the workspace so the view gets maximum exposure */
    int dx = bbox.x + bbox.width / 2;
    int dy = bbox.y + bbox.height / 2;

    int dvx = std::floor(1.0 * dx / g.width);
    int dvy = std::floor(1.0 * dy / g.height);
    auto cws = workspace->get_current_workspace();

    change_viewport_signal data;

    data.carried_out = false;
    data.old_viewport = cws;
    data.new_viewport = cws + wf_point{dvx, dvy};

    emit_signal("set-workspace-request", &data);
    if (!data.carried_out)
        workspace->set_workspace(data.new_viewport);

    return true;
}

void wf::output_impl_t::update_active_view(wayfire_view v)
{
    this->active_view = v;
    if (this == wf::get_core().get_active_output())
        wf::get_core().set_active_view(v);
}

void wf::output_impl_t::focus_view(wayfire_view v, bool raise)
{
    if (v && workspace->get_view_layer(v) < wf::get_core().get_focused_layer())
    {
        auto active_view = get_active_view();
        if (active_view && active_view->get_app_id().find("$unfocus") == 0)
            return focus_view(nullptr, false);

        LOGD("Denying focus request for a view from a lower layer than the"
            " focused layer");
        return;
    }

    if (!v || !v->is_mapped())
    {
        update_active_view(nullptr);
        return;
    }

    /* If no keyboard focus surface is set, then we don't want to focus the view */
    if (v->get_keyboard_focus_surface() || interactive_view_from_view(v.get()))
    {
        /* We must make sure the view which gets focus is visible on the
         * current workspace */
        if (v->minimized)
            v->minimize_request(false);

        update_active_view(v);
        if (raise)
            workspace->bring_to_front(v);

        focus_view_signal data;
        data.view = v;
        emit_signal("focus-view", &data);
    }
}

wayfire_view wf::output_t::get_top_view() const
{
    auto views = workspace->get_views_on_workspace(workspace->get_current_workspace(),
        LAYER_WORKSPACE, false);

    return views.empty() ? nullptr : views[0];
}

wayfire_view wf::output_impl_t::get_active_view() const
{
    return active_view;
}

bool wf::output_impl_t::can_activate_plugin(const plugin_grab_interface_uptr& owner,
    bool ignore_inhibit)
{
    if (!owner)
        return false;

    if (this->inhibited && !ignore_inhibit)
        return false;

    if (active_plugins.find(owner.get()) != active_plugins.end())
        return true;

    for(auto act_owner : active_plugins)
    {
        bool compatible =
            ((act_owner->capabilities & owner->capabilities) == 0);
        if (!compatible)
            return false;
    }

    return true;
}

bool wf::output_impl_t::activate_plugin(const plugin_grab_interface_uptr& owner,
    bool ignore_inhibit)
{
    if (!can_activate_plugin(owner, ignore_inhibit))
        return false;

    if (active_plugins.find(owner.get()) != active_plugins.end())
        LOGD("output %s: activate plugin %s again", handle->name, owner->name.c_str());
    else
        LOGD("output %s: activate plugin %s", handle->name, owner->name.c_str());

    active_plugins.insert(owner.get());
    return true;
}

bool wf::output_impl_t::deactivate_plugin(
    const plugin_grab_interface_uptr& owner)
{
    auto it = active_plugins.find(owner.get());
    if (it == active_plugins.end())
        return true;

    active_plugins.erase(it);
    LOGD("output %s: deactivate plugin %s", handle->name, owner->name.c_str());

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
        if (act && act->name == name)
            return true;

    return false;
}

wf::plugin_grab_interface_t* wf::output_impl_t::get_input_grab_interface()
{
    for (auto p : active_plugins)
        if (p && p->is_grabbed())
            return p;

    return nullptr;
}

void wf::output_impl_t::inhibit_plugins()
{
    this->inhibited = true;

    std::vector<wf::plugin_grab_interface_t*> ifaces;
    for (auto p : active_plugins)
    {
        if (p->callbacks.cancel)
            ifaces.push_back(p);
    }

    for (auto p : ifaces)
        p->callbacks.cancel();
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
wf_binding *wf::output_t::add_key(option_sptr_t<keybinding_t> key, key_callback *callback)
{
    return wf::get_core_impl().input->new_binding(WF_BINDING_KEY, key, this, callback);
}

wf_binding *wf::output_t::add_axis(option_sptr_t<keybinding_t> axis, axis_callback *callback)
{
    return wf::get_core_impl().input->new_binding(WF_BINDING_AXIS, axis, this, callback);
}

wf_binding *wf::output_t::add_touch(option_sptr_t<keybinding_t> mod, touch_callback *callback)
{
    return wf::get_core_impl().input->new_binding(WF_BINDING_TOUCH, mod, this, callback);
}

wf_binding *wf::output_t::add_button(option_sptr_t<buttonbinding_t> button,
        button_callback *callback)
{
    return wf::get_core_impl().input->new_binding(WF_BINDING_BUTTON, button,
        this, callback);
}

wf_binding *wf::output_t::add_gesture(option_sptr_t<touchgesture_t> gesture,
        gesture_callback *callback)
{
    return wf::get_core_impl().input->new_binding(WF_BINDING_GESTURE, gesture,
        this, callback);
}

wf_binding *wf::output_t::add_activator(
    option_sptr_t<activatorbinding_t> activator, activator_callback *callback)
{
    return wf::get_core_impl().input->new_binding(WF_BINDING_ACTIVATOR, activator,
        this, callback);
}

void wf::output_t::rem_binding(wf_binding *binding)
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
            mask |= (1 << i);
    }

    return mask;
}
}
