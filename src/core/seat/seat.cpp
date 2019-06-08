#include "seat.hpp"
#include "opengl.hpp"
#include "../core-impl.hpp"
#include "input-manager.hpp"
#include "render-manager.hpp"
#include "output-layout.hpp"
#include "debug.hpp"
#include "signal-definitions.hpp"

extern "C"
{
#include <wlr/backend/libinput.h>
}

wf_drag_icon::wf_drag_icon(wlr_drag_icon *ic)
    : wf::wlr_child_surface_base_t(nullptr, this), icon(ic)
{
    on_map.set_callback([&] (void*) { this->map(icon->surface); });
    on_unmap.set_callback([&] (void*) { this->unmap(); });
    on_destroy.set_callback([&] (void*) {
        /* we don't dec_keep_count() because the surface memory is
         * managed by the unique_ptr */
        wf::get_core_impl().input->drag_icon = nullptr;
        wf::get_core().emit_signal("drag-stopped", nullptr);
    });

    on_map.connect(&icon->events.map);
    on_unmap.connect(&icon->events.unmap);
    on_destroy.connect(&icon->events.destroy);
}

wf_point wf_drag_icon::get_offset()
{
    auto pos = icon->drag->grab_type == WLR_DRAG_GRAB_KEYBOARD_TOUCH ?
        wf::get_core().get_touch_position(icon->drag->touch_id) :
            wf::get_core().get_cursor_position();

    GetTuple(x, y, pos);

    if (is_mapped())
    {
        x += icon->surface->sx;
        y += icon->surface->sy;
    }

    return {x, y};
}

void wf_drag_icon::damage()
{
    wlr_box box = {0, 0, get_size().width, get_size().height};
    damage_surface_box(box);
}

void wf_drag_icon::damage_surface_box(const wlr_box& box)
{
    if (!is_mapped())
        return;

    wlr_box damage = box;
    damage.x += get_offset().x;
    damage.y += get_offset().y;

    for (auto& output : wf::get_core().output_layout->get_outputs())
    {
        auto output_geometry = output->get_layout_geometry();
        if (output_geometry & damage)
        {
            auto local = damage;
            local.x -= output_geometry.x;
            local.y -= output_geometry.y;

            const auto& fb = output->render->get_target_framebuffer();
            output->render->damage(fb.damage_box_from_geometry_box(local));
        }
    }
}

void input_manager::validate_drag_request(wlr_seat_request_start_drag_event *ev)
{
    auto seat = wf::get_core().get_current_seat();

    if (wlr_seat_validate_pointer_grab_serial(seat, ev->origin, ev->serial))
    {
        wlr_seat_start_pointer_drag(seat, ev->drag, ev->serial);
        return;
    }

    struct wlr_touch_point *point;
    if (wlr_seat_validate_touch_grab_serial(seat, ev->origin, ev->serial, &point))
    {
        wlr_seat_start_touch_drag(seat, ev->drag, ev->serial, point);
        return;
    }

    log_debug("Ignoring start_drag request: "
        "could not validate pointer or touch serial %" PRIu32, ev->serial);
    wlr_data_source_destroy(ev->drag->source);
}

void input_manager::update_drag_icon()
{
    if (drag_icon && drag_icon->is_mapped())
        drag_icon->damage();
}

void input_manager::create_seat()
{
    seat = wlr_seat_create(wf::get_core().display, "default");
    cursor = std::make_unique<wf_cursor> ();

    request_set_cursor.set_callback([&] (void* data) {
        auto ev = static_cast<wlr_seat_pointer_request_set_cursor_event*> (data);
        wf::get_core_impl().input->cursor->set_cursor(ev);
    });
    request_set_cursor.connect(&seat->events.request_set_cursor);

    request_start_drag.set_callback([&] (void *data) {
        auto ev = static_cast<wlr_seat_request_start_drag_event*> (data);
        validate_drag_request(ev);
    });
    request_start_drag.connect(&seat->events.request_start_drag);

    start_drag.set_callback([&] (void *data) {
        auto d = static_cast<wlr_drag*> (data);
        wf::get_core_impl().input->drag_icon =
            std::make_unique<wf_drag_icon> (d->icon);
        wf::get_core().emit_signal("drag-started", nullptr);
    });
    start_drag.connect(&seat->events.start_drag);

    request_set_selection.set_callback([&] (void *data) {
        auto ev = static_cast<wlr_seat_request_set_selection_event*> (data);
        wlr_seat_set_selection(wf::get_core().get_current_seat(),
            ev->source, ev->serial);
    });
    request_set_selection.connect(&seat->events.request_set_selection);

    request_set_primary_selection.set_callback([&] (void *data) {
        auto ev =
            static_cast<wlr_seat_request_set_primary_selection_event*> (data);
        wlr_seat_set_primary_selection(wf::get_core().get_current_seat(),
            ev->source, ev->serial);
    });
    request_set_primary_selection.connect(
        &seat->events.request_set_primary_selection);
}

namespace wf
{
    wlr_input_device* input_device_t::get_wlr_handle()
    {
        return handle;
    }

    bool input_device_t::set_enabled(bool enabled)
    {
        if (enabled == is_enabled())
            return true;

        if (!wlr_input_device_is_libinput(handle))
            return false;

        auto dev = wlr_libinput_get_device_handle(handle);
        assert(dev);

        libinput_device_config_send_events_set_mode(dev,
            enabled ?  LIBINPUT_CONFIG_SEND_EVENTS_ENABLED :
                LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);

        return true;
    }

    bool input_device_t::is_enabled()
    {
        /* Currently no support for enabling/disabling non-libinput devices */
        if (!wlr_input_device_is_libinput(handle))
            return true;

        auto dev = wlr_libinput_get_device_handle(handle);
        assert(dev);

        auto mode = libinput_device_config_send_events_get_mode(dev);
        return mode == LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
    }

    input_device_t::input_device_t(wlr_input_device *handle)
    {
        this->handle = handle;
    }
}


wf_input_device_internal::config_t wf_input_device_internal::config;
void wf_input_device_internal::config_t::load(wayfire_config *config)
{
    auto section = (*config)["input"];
    mouse_cursor_speed              = section->get_option("mouse_cursor_speed", "0");
    touchpad_cursor_speed           = section->get_option("touchpad_cursor_speed", "0");
    touchpad_tap_enabled            = section->get_option("tap_to_click", "1");
    touchpad_click_method           = section->get_option("click_method", "default");
    touchpad_scroll_method          = section->get_option("scroll_method", "default");
    touchpad_dwt_enabled            = section->get_option("disable_while_typing", "0");
    touchpad_dwmouse_enabled        = section->get_option("disable_touchpad_while_mouse", "0");
    touchpad_natural_scroll_enabled = section->get_option("natural_scroll", "0");
}

wf_input_device_internal::wf_input_device_internal(wlr_input_device *dev)
    : wf::input_device_t(dev)
{
    update_options();

    on_destroy.set_callback([&] (void*) {
        wf::get_core_impl().input->handle_input_destroyed(this->get_wlr_handle());
    });
    on_destroy.connect(&dev->events.destroy);

    if (dev->type == WLR_INPUT_DEVICE_SWITCH)
    {
        on_switch.set_callback([&] (void *data) {
            this->handle_switched((wlr_event_switch_toggle*) data);
        });
        on_switch.connect(&dev->switch_device->events.toggle);
    }
}

void wf_input_device_internal::handle_switched(wlr_event_switch_toggle *ev)
{
    wf::switch_signal data;
    data.device = nonstd::make_observer(this);
    data.state = (ev->switch_state == WLR_SWITCH_STATE_ON);

    std::string event_name;
    switch (ev->switch_type)
    {
        case WLR_SWITCH_TYPE_TABLET_MODE:
            event_name = "tablet-mode";
            break;
        case WLR_SWITCH_TYPE_LID:
            event_name = "lid-state";
            break;
    }

    wf::get_core().emit_signal(event_name, &data);
}

void wf_input_device_internal::update_options()
{
    /* We currently support options only for libinput devices */
    if (!wlr_input_device_is_libinput(get_wlr_handle()))
        return;

    auto dev = wlr_libinput_get_device_handle(get_wlr_handle());
    assert(dev);

    /* we are configuring a touchpad */
    if (libinput_device_config_tap_get_finger_count(dev) > 0)
    {
        libinput_device_config_accel_set_speed(dev,
            config.touchpad_cursor_speed->as_cached_double());

        libinput_device_config_tap_set_enabled(dev,
            config.touchpad_tap_enabled->as_cached_int() ?
            LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);

        if (config.touchpad_click_method->as_string() == "default") {
            libinput_device_config_click_set_method(dev,
                libinput_device_config_click_get_default_method(dev));
        } else if (config.touchpad_click_method->as_string() == "none") {
            libinput_device_config_click_set_method(dev,
                LIBINPUT_CONFIG_CLICK_METHOD_NONE);
        } else if (config.touchpad_click_method->as_string() == "button-areas") {
            libinput_device_config_click_set_method(dev,
                LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS);
        } else if (config.touchpad_click_method->as_string() == "clickfinger") {
            libinput_device_config_click_set_method(dev,
                LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);
        }

        if (config.touchpad_scroll_method->as_string() == "default") {
            libinput_device_config_scroll_set_method(dev,
                libinput_device_config_scroll_get_default_method(dev));
        } else if (config.touchpad_scroll_method->as_string() == "none") {
            libinput_device_config_scroll_set_method(dev,
                LIBINPUT_CONFIG_SCROLL_NO_SCROLL);
        } else if (config.touchpad_scroll_method->as_string() == "two-finger") {
            libinput_device_config_scroll_set_method(dev,
                LIBINPUT_CONFIG_SCROLL_2FG);
        } else if (config.touchpad_scroll_method->as_string() == "edge") {
            libinput_device_config_scroll_set_method(dev,
                LIBINPUT_CONFIG_SCROLL_EDGE);
        } else if (config.touchpad_scroll_method->as_string() == "on-button-down") {
            libinput_device_config_scroll_set_method(dev,
                LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
        }

        libinput_device_config_dwt_set_enabled(dev,
            config.touchpad_dwt_enabled->as_cached_int() ?
            LIBINPUT_CONFIG_DWT_ENABLED : LIBINPUT_CONFIG_DWT_DISABLED);

        libinput_device_config_send_events_set_mode(dev,
            config.touchpad_dwmouse_enabled->as_cached_int() ?
            LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE
                : LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);

        if (libinput_device_config_scroll_has_natural_scroll(dev) > 0)
        {
            libinput_device_config_scroll_set_natural_scroll_enabled(dev,
                    (bool)config.touchpad_natural_scroll_enabled->as_cached_int());
        }
    } else {
        libinput_device_config_accel_set_speed(dev,
            config.mouse_cursor_speed->as_cached_double());
    }
}

wf_point get_surface_relative_coords(wf::surface_interface_t *surface,
    const wf_point& point)
{
    auto view =
        dynamic_cast<wf::view_interface_t*> (surface->get_main_surface());
    auto local = view->global_to_local_point(point, surface);
    return local;
}
