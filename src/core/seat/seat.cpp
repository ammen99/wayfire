#include "seat.hpp"
#include "wayfire/opengl.hpp"
#include "../core-impl.hpp"
#include "input-manager.hpp"
#include "wayfire/render-manager.hpp"
#include "wayfire/output-layout.hpp"
#include <wayfire/util/log.hpp>
#include "wayfire/signal-definitions.hpp"

extern "C"
{
#include <wlr/backend/libinput.h>
}

wf_drag_icon::wf_drag_icon(wlr_drag_icon *ic)
    : wf::wlr_child_surface_base_t(this), icon(ic)
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

wf::point_t wf_drag_icon::get_offset()
{
    auto pos = icon->drag->grab_type == WLR_DRAG_GRAB_KEYBOARD_TOUCH ?
        wf::get_core().get_touch_position(icon->drag->touch_id) :
            wf::get_core().get_cursor_position();

    if (is_mapped())
    {
        pos.x += icon->surface->sx;
        pos.y += icon->surface->sy;
    }

    return {(int)pos.x, (int)pos.y};
}

void wf_drag_icon::damage()
{
    // damage previous position
    damage_surface_box_global(last_box);

    // damage new position
    last_box = {0, 0, get_size().width, get_size().height};
    last_box = last_box + get_offset();
    damage_surface_box_global(last_box);
}

void wf_drag_icon::damage_surface_box(const wlr_box& box)
{
    if (!is_mapped())
        return;

    damage_surface_box_global(box + this->get_offset());
}

void wf_drag_icon::damage_surface_box_global(const wlr_box& rect)
{
    for (auto& output : wf::get_core().output_layout->get_outputs())
    {
        auto output_geometry = output->get_layout_geometry();
        if (output_geometry & rect)
        {
            auto local = rect;
            local.x -= output_geometry.x;
            local.y -= output_geometry.y;
            output->render->damage(local);
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

    LOGD("Ignoring start_drag request: ",
        "could not validate pointer or touch serial ", ev->serial);
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
    lpointer = std::make_unique<wf::LogicalPointer> (
        nonstd::make_observer(this));

    request_set_cursor.set_callback([&] (void* data) {
        auto ev = static_cast<wlr_seat_pointer_request_set_cursor_event*> (data);
        wf::get_core_impl().input->cursor->set_cursor(ev, true);
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

wf_input_device_internal::wf_input_device_internal(wlr_input_device *dev)
    : wf::input_device_t(dev)
{
    on_destroy.set_callback([&] (void*) {
        wf::get_core_impl().input->handle_input_destroyed(this->get_wlr_handle());
    });
    on_destroy.connect(&dev->events.destroy);
}

wf::pointf_t get_surface_relative_coords(wf::surface_interface_t *surface,
    const wf::pointf_t& point)
{
    auto og = surface->get_output()->get_layout_geometry();
    auto local = point;
    local.x -= og.x;
    local.y -= og.y;

    auto view =
        dynamic_cast<wf::view_interface_t*> (surface->get_main_surface());
    return view->global_to_local_point(local, surface);
}
