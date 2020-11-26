#include "tablet.hpp"
#include "../core-impl.hpp"
#include "../wm.hpp"
#include "pointer.hpp"
#include "cursor.hpp"
#include "input-manager.hpp"
#include <wayfire/signal-definitions.hpp>
#include <linux/input-event-codes.h>

/* --------------------- Tablet tool implementation ------------------------- */
wf::tablet_tool_t::tablet_tool_t(wlr_tablet_tool *tool,
    wlr_tablet_v2_tablet *tablet)
{
    this->tablet_v2 = tablet;

    /* Initialize tool_v2 */
    this->tool = tool;
    this->tool->data = this;

    auto& core = wf::get_core_impl();
    this->tool_v2 = wlr_tablet_tool_create(core.protocols.tablet_v2,
        core.get_current_seat(), tool);

    /* Free memory when the tool is destroyed */
    this->on_destroy.set_callback([=] (void*)
    {
        this->tool->data = nullptr;
        delete this;
    });
    this->on_destroy.connect(&tool->events.destroy);

    /* Ungrab surface, and update focused surface if a surface is unmapped,
     * we don't want to end up with a reference to unfocused or a destroyed
     * surface. */
    on_surface_map_state_changed.set_callback([=] (signal_data_t *data)
    {
        auto ev = static_cast<surface_map_state_changed_signal*>(data);
        if (!ev->surface->is_mapped() && (ev->surface == this->grabbed_surface))
        {
            this->grabbed_surface = nullptr;
        }

        update_tool_position();
    });

    wf::get_core().connect_signal("surface-mapped",
        &on_surface_map_state_changed);
    wf::get_core().connect_signal("surface-unmapped",
        &on_surface_map_state_changed);

    on_views_updated.set_callback([&] (wf::signal_data_t *data)
    {
        update_tool_position();
    });

    wf::get_core().connect_signal("output-stack-order-changed", &on_views_updated);
    wf::get_core().connect_signal("view-geometry-changed", &on_views_updated);

    /* Just pass cursor set requests to core, but translate them to
     * regular pointer set requests */
    on_set_cursor.set_callback([=] (void *data)
    {
        if (!this->is_active)
        {
            return;
        }

        auto ev = static_cast<wlr_tablet_v2_event_cursor*>(data);
        // validate request
        wlr_seat_client *tablet_client = nullptr;
        if (tool_v2->focused_surface)
        {
            tablet_client = wlr_seat_client_for_wl_client(
                wf::get_core().get_current_seat(),
                wl_resource_get_client(tool_v2->focused_surface->resource));
        }

        if (tablet_client != ev->seat_client)
        {
            return;
        }

        wlr_seat_pointer_request_set_cursor_event pev;
        pev.surface   = ev->surface;
        pev.hotspot_x = ev->hotspot_x;
        pev.hotspot_y = ev->hotspot_y;
        pev.serial    = ev->serial;
        pev.seat_client = ev->seat_client;
        wf::get_core_impl().seat->cursor->set_cursor(&pev, false);
    });
    on_set_cursor.connect(&tool_v2->events.set_cursor);
}

wf::tablet_tool_t::~tablet_tool_t()
{
    tool->data = NULL;
}

void wf::tablet_tool_t::update_tool_position()
{
    if (!is_active)
    {
        return;
    }

    auto& core  = wf::get_core_impl();
    auto& input = core.input;
    auto gc     = core.get_cursor_position();

    /* XXX: tablet input works only with programs, Wayfire itself doesn't do
     * anything useful with it */
    if (core.input->input_grabbed())
    {
        return;
    }

    /* Figure out what surface is under the tool */
    wf::pointf_t local; // local to the surface
    wf::surface_interface_t *surface = nullptr;
    if (this->grabbed_surface)
    {
        surface = this->grabbed_surface;
        local   = get_surface_relative_coords(surface, gc);
    } else
    {
        surface = input->input_surface_at(gc, local);
    }

    set_focus(surface);

    /* If focus is a wlr surface, send position */
    wlr_surface *next_focus = surface ? surface->get_wlr_surface() : nullptr;
    if (next_focus)
    {
        wlr_tablet_v2_tablet_tool_notify_motion(tool_v2, local.x, local.y);
    }
}

void wf::tablet_tool_t::set_focus(wf::surface_interface_t *surface)
{
    /* Unfocus old surface */
    if ((surface != this->proximity_surface) && this->proximity_surface)
    {
        wlr_tablet_v2_tablet_tool_notify_proximity_out(tool_v2);
        this->proximity_surface = nullptr;
    }

    /* Set the new focus, if it is a wlr surface */
    wf::get_core_impl().seat->ensure_input_surface(surface);
    wlr_surface *next_focus = surface ? surface->get_wlr_surface() : nullptr;
    if (next_focus && wlr_surface_accepts_tablet_v2(tablet_v2, next_focus))
    {
        this->proximity_surface = surface;
        wlr_tablet_v2_tablet_tool_notify_proximity_in(
            tool_v2, tablet_v2, next_focus);
    }

    if (!next_focus)
    {
        wf::get_core().set_cursor("default");
    }
}

void wf::tablet_tool_t::passthrough_axis(wlr_event_tablet_tool_axis *ev)
{
    if (ev->updated_axes & WLR_TABLET_TOOL_AXIS_PRESSURE)
    {
        wlr_tablet_v2_tablet_tool_notify_pressure(tool_v2, ev->pressure);
    }

    if (ev->updated_axes & WLR_TABLET_TOOL_AXIS_DISTANCE)
    {
        wlr_tablet_v2_tablet_tool_notify_distance(tool_v2, ev->distance);
    }

    if (ev->updated_axes & WLR_TABLET_TOOL_AXIS_ROTATION)
    {
        wlr_tablet_v2_tablet_tool_notify_rotation(tool_v2, ev->rotation);
    }

    if (ev->updated_axes & WLR_TABLET_TOOL_AXIS_SLIDER)
    {
        wlr_tablet_v2_tablet_tool_notify_slider(tool_v2, ev->slider);
    }

    if (ev->updated_axes & WLR_TABLET_TOOL_AXIS_WHEEL)
    {
        wlr_tablet_v2_tablet_tool_notify_wheel(tool_v2, ev->wheel_delta, 0);
    }

    /* Update tilt, use old values if no new values are provided */
    if (ev->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_X)
    {
        tilt_x = ev->tilt_x;
    }

    if (ev->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_Y)
    {
        tilt_y = ev->tilt_y;
    }

    if (ev->updated_axes &
        (WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y))
    {
        wlr_tablet_v2_tablet_tool_notify_tilt(tool_v2, tilt_x, tilt_y);
    }
}

void wf::tablet_tool_t::handle_tip(wlr_event_tablet_tool_tip *ev)
{
    /* Nothing to do without a proximity surface */
    if (!this->proximity_surface)
    {
        return;
    }

    if (ev->state == WLR_TABLET_TOOL_TIP_DOWN)
    {
        wlr_send_tablet_v2_tablet_tool_down(tool_v2);
        this->grabbed_surface = this->proximity_surface;

        /* Try to focus the view under the tool */
        auto view = dynamic_cast<wf::view_interface_t*>(
            this->proximity_surface->get_main_surface());
        if (view)
        {
            wf::get_core().focus_output(view->get_output());
            wm_focus_request data;
            data.surface = this->proximity_surface;
            view->get_output()->emit_signal("wm-focus-request", &data);
        }
    } else
    {
        wlr_send_tablet_v2_tablet_tool_up(tool_v2);
        this->grabbed_surface = nullptr;
    }
}

void wf::tablet_tool_t::handle_button(wlr_event_tablet_tool_button *ev)
{
    wlr_tablet_v2_tablet_tool_notify_button(tool_v2,
        (zwp_tablet_pad_v2_button_state)ev->button,
        (zwp_tablet_pad_v2_button_state)ev->state);
}

void wf::tablet_tool_t::handle_proximity(wlr_event_tablet_tool_proximity *ev)
{
    if (ev->state == WLR_TABLET_TOOL_PROXIMITY_OUT)
    {
        set_focus(nullptr);
        is_active = false;
    } else
    {
        is_active = true;
        update_tool_position();
    }
}

/* ----------------------- Tablet implementation ---------------------------- */
wf::tablet_t::tablet_t(wlr_cursor *cursor, wlr_input_device *dev) :
    input_device_impl_t(dev)
{
    this->handle = dev->tablet;
    this->handle->data = this;
    this->cursor = cursor;

    auto& core = wf::get_core_impl();
    tablet_v2 = wlr_tablet_create(core.protocols.tablet_v2,
        core.get_current_seat(), dev);
}

wf::tablet_t::~tablet_t()
{
    this->handle->data = NULL;
}

wf::tablet_tool_t*wf::tablet_t::ensure_tool(wlr_tablet_tool *tool)
{
    if (tool->data == NULL)
    {
        new wf::tablet_tool_t(tool, tablet_v2);
    }

    return (wf::tablet_tool_t*)tool->data;
}

void wf::tablet_t::handle_tip(wlr_event_tablet_tool_tip *ev)
{
    auto& input = wf::get_core_impl().input;
    if (input->input_grabbed())
    {
        /* Simulate buttons, in case some application started moving */
        if (input->active_grab->callbacks.pointer.button)
        {
            uint32_t state = ev->state == WLR_TABLET_TOOL_TIP_DOWN ?
                WLR_BUTTON_PRESSED : WLR_BUTTON_RELEASED;
            input->active_grab->callbacks.pointer.button(BTN_LEFT, state);
        }

        return;
    }

    auto tool = ensure_tool(ev->tool);
    tool->handle_tip(ev);
}

void wf::tablet_t::handle_axis(wlr_event_tablet_tool_axis *ev)
{
    auto& input = wf::get_core_impl().input;

    /* Update cursor position */
    switch (ev->tool->type)
    {
      case WLR_TABLET_TOOL_TYPE_MOUSE:
        wlr_cursor_move(cursor, ev->device, ev->dx, ev->dy);
        break;

      default:
        double x = (ev->updated_axes & WLR_TABLET_TOOL_AXIS_X) ? ev->x : NAN;
        double y = (ev->updated_axes & WLR_TABLET_TOOL_AXIS_Y) ? ev->y : NAN;
        wlr_cursor_warp_absolute(cursor, ev->device, x, y);
    }

    if (input->input_grabbed())
    {
        /* Simulate movement */
        if (input->active_grab->callbacks.pointer.motion)
        {
            auto gc = wf::get_core().get_cursor_position();
            input->active_grab->callbacks.pointer.motion(gc.x, gc.y);
        }

        return;
    }

    /* Update focus */
    auto tool = ensure_tool(ev->tool);
    tool->update_tool_position();
    tool->passthrough_axis(ev);
}

void wf::tablet_t::handle_button(wlr_event_tablet_tool_button *ev)
{
    /* Pass to the tool */
    ensure_tool(ev->tool)->handle_button(ev);
}

void wf::tablet_t::handle_proximity(wlr_event_tablet_tool_proximity *ev)
{
    ensure_tool(ev->tool)->handle_proximity(ev);

    auto& impl = wf::get_core_impl();

    /* Show appropriate cursor */
    if (ev->state == WLR_TABLET_TOOL_PROXIMITY_OUT)
    {
        impl.set_cursor("default");
        impl.seat->lpointer->set_enable_focus(true);
    } else
    {
        wf::get_core().set_cursor("crosshair");
        impl.seat->lpointer->set_enable_focus(false);
    }
}
