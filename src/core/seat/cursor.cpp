#include "cursor.hpp"
#include "touch.hpp"
#include "../core-impl.hpp"
#include "../../view/view-impl.hpp"
#include "input-manager.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wayfire/compositor-surface.hpp"
#include "wayfire/output-layout.hpp"
#include "tablet.hpp"
#include "wayfire/signal-definitions.hpp"

extern "C" {
#include <wlr/util/region.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
}

wf_cursor::wf_cursor()
{
    cursor = wlr_cursor_create();

    wlr_cursor_attach_output_layout(cursor,
        wf::get_core().output_layout->get_handle());

    wlr_cursor_map_to_output(cursor, NULL);
    wlr_cursor_warp(cursor, NULL, cursor->x, cursor->y);

    setup_listeners();
    init_xcursor();

    config_reloaded = [=] (wf::signal_data_t*)
    {
        init_xcursor();
    };

    wf::get_core().connect_signal("reload-config", &config_reloaded);
}

void wf_cursor::setup_listeners()
{
    auto& core = wf::get_core_impl();

    /* Dispatch pointer events to the LogicalPointer */
    on_frame.set_callback([&] (void*)
    {
        core.input->lpointer->handle_pointer_frame();
        wlr_idle_notify_activity(core.protocols.idle,
            core.get_current_seat());
    });
    on_frame.connect(&cursor->events.frame);

#define setup_passthrough_callback(evname) \
    on_ ## evname.set_callback([&] (void *data) { \
        auto ev = static_cast<wlr_event_pointer_ ## evname*>(data); \
        emit_device_event_signal("pointer_" #evname, ev); \
        core.input->lpointer->handle_pointer_ ## evname(ev); \
        wlr_idle_notify_activity(core.protocols.idle, core.get_current_seat()); \
    }); \
    on_ ## evname.connect(&cursor->events.evname);

    setup_passthrough_callback(button);
    setup_passthrough_callback(motion);
    setup_passthrough_callback(motion_absolute);
    setup_passthrough_callback(axis);
    setup_passthrough_callback(swipe_begin);
    setup_passthrough_callback(swipe_update);
    setup_passthrough_callback(swipe_end);
    setup_passthrough_callback(pinch_begin);
    setup_passthrough_callback(pinch_update);
    setup_passthrough_callback(pinch_end);
#undef setup_passthrough_callback

    /**
     * All tablet events are directly sent to the tablet device, it should
     * manage them
     */
#define setup_tablet_callback(evname) \
    on_tablet_ ## evname.set_callback([&] (void *data) { \
        auto ev = static_cast<wlr_event_tablet_tool_ ## evname*>(data); \
        emit_device_event_signal("tablet_" #evname, ev); \
        if (ev->device->tablet->data) { \
            auto tablet = \
                static_cast<wf::tablet_t*>(ev->device->tablet->data); \
            tablet->handle_ ## evname(ev); \
        } \
        wlr_idle_notify_activity(wf::get_core().protocols.idle, \
        wf::get_core().get_current_seat()); \
    }); \
    on_tablet_ ## evname.connect(&cursor->events.tablet_tool_ ## evname);

    setup_tablet_callback(tip);
    setup_tablet_callback(axis);
    setup_tablet_callback(button);
    setup_tablet_callback(proximity);
#undef setup_tablet_callback
}

void wf_cursor::init_xcursor()
{
    std::string theme = wf::option_wrapper_t<std::string>("input/cursor_theme");
    int size = wf::option_wrapper_t<int>("input/cursor_size");
    auto theme_ptr = (theme == "default") ? NULL : theme.c_str();

    if (xcursor)
    {
        wlr_xcursor_manager_destroy(xcursor);
    }

    xcursor = wlr_xcursor_manager_create(theme_ptr, size);

    load_xcursor_scale(1);
    for (auto& wo : wf::get_core().output_layout->get_current_configuration())
    {
        if (wo.second.source & wf::OUTPUT_IMAGE_SOURCE_SELF)
        {
            load_xcursor_scale(wo.first->scale);
        }
    }

    set_cursor("default");

    auto default_cursor =
        wlr_xcursor_manager_get_xcursor(xcursor, "left_ptr", 1);
    if (default_cursor)
    {
        wf::xwayland_set_cursor(default_cursor->images[0]);
    }
}

void wf_cursor::load_xcursor_scale(float scale)
{
    wlr_xcursor_manager_load(xcursor, scale);
}

void wf_cursor::attach_device(wlr_input_device *device)
{
    wlr_cursor_attach_input_device(cursor, device);
}

void wf_cursor::detach_device(wlr_input_device *device)
{
    wlr_cursor_detach_input_device(cursor, device);
}

void wf_cursor::set_cursor(std::string name)
{
    if (name == "default")
    {
        name = "left_ptr";
    }

    wlr_xcursor_manager_set_cursor_image(xcursor, name.c_str(), cursor);
}

void wf_cursor::hide_cursor()
{
    wlr_cursor_set_surface(cursor, NULL, 0, 0);
}

void wf_cursor::warp_cursor(wf::pointf_t point)
{
    wlr_cursor_warp_closest(cursor, NULL, point.x, point.y);
}

wf::pointf_t wf_cursor::get_cursor_position()
{
    return {cursor->x, cursor->y};
}

void wf_cursor::set_cursor(wlr_seat_pointer_request_set_cursor_event *ev,
    bool validate_request)
{
    auto& input = wf::get_core_impl().input;
    if (validate_request)
    {
        auto pointer_client = input->seat->pointer_state.focused_client;
        if (pointer_client != ev->seat_client)
        {
            return;
        }
    }

    if (!wf::get_core_impl().input->input_grabbed())
    {
        wlr_cursor_set_surface(cursor, ev->surface,
            ev->hotspot_x, ev->hotspot_y);
    }
}

wf_cursor::~wf_cursor()
{
    wf::get_core().disconnect_signal("reload-config", &config_reloaded);
}
