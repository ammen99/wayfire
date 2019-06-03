#include <cassert>
#include <algorithm>
#include <libinput.h>

#include <iostream>

extern "C"
{
#include <wlr/types/wlr_seat.h>
}

#include "input-inhibit.hpp"
#include "signal-definitions.hpp"
#include "../core-impl.hpp"
#include "touch.hpp"
#include "keyboard.hpp"
#include "cursor.hpp"
#include "input-manager.hpp"
#include "output-layout.hpp"
#include "workspace-manager.hpp"
#include "debug.hpp"

bool input_manager::is_touch_enabled()
{
    return touch_count > 0;
}

void input_manager::update_capabilities()
{
    uint32_t cap = 0;
    if (pointer_count)
        cap |= WL_SEAT_CAPABILITY_POINTER;
    if (keyboards.size())
        cap |= WL_SEAT_CAPABILITY_KEYBOARD;
    if (touch_count)
        cap |= WL_SEAT_CAPABILITY_TOUCH;

    wlr_seat_set_capabilities(seat, cap);
}

void input_manager::handle_new_input(wlr_input_device *dev)
{
    log_info("handle new input: %s, default mapping: %s",
        dev->name, dev->output_name);
    input_devices.push_back(std::make_unique<wf_input_device_internal> (dev));

    if (dev->type == WLR_INPUT_DEVICE_KEYBOARD)
    {
        keyboards.push_back(
            std::make_unique<wf_keyboard> (dev, wf::get_core().config));
    }

    if (dev->type == WLR_INPUT_DEVICE_POINTER)
    {
        cursor->attach_device(dev);
        pointer_count++;
    }

    if (dev->type == WLR_INPUT_DEVICE_TOUCH)
    {
        touch_count++;
        if (!our_touch)
            our_touch = std::unique_ptr<wf_touch> (new wf_touch(cursor->cursor));

        our_touch->add_device(dev);
    }

    auto section = wf::get_core().config->get_section(nonull(dev->name));
    auto mapped_output = section->get_option("output",
        nonull(dev->output_name))->as_string();

    auto wo = wf::get_core().output_layout->find_output(mapped_output);
    if (wo)
        wlr_cursor_map_input_to_output(cursor->cursor, dev, wo->handle);

    update_capabilities();
}

void input_manager::handle_input_destroyed(wlr_input_device *dev)
{
    log_info("remove input: %s", dev->name);

    auto it = std::remove_if(input_devices.begin(), input_devices.end(),
        [=] (const std::unique_ptr<wf_input_device_internal>& idev) {
            return idev->get_wlr_handle() == dev;
        });
    input_devices.erase(it, input_devices.end());

    if (dev->type == WLR_INPUT_DEVICE_KEYBOARD)
    {
        auto it = std::remove_if(keyboards.begin(), keyboards.end(),
            [=] (const std::unique_ptr<wf_keyboard>& kbd) { return kbd->device == dev; });

        keyboards.erase(it, keyboards.end());
    }

    if (dev->type == WLR_INPUT_DEVICE_POINTER)
    {
        cursor->detach_device(dev);
        pointer_count--;
    }

    if (dev->type == WLR_INPUT_DEVICE_TOUCH)
        touch_count--;

    update_capabilities();
}

input_manager::input_manager()
{
    input_device_created.set_callback([&] (void *data) {
        auto dev = static_cast<wlr_input_device*> (data);
        assert(dev);
        wf::get_core_impl().input->handle_new_input(dev);
    });
    input_device_created.connect(&wf::get_core().backend->events.new_input);

    create_seat();
    surface_map_state_changed = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<_surface_map_state_changed_signal*> (data);
        if (ev && cursor->grabbed_surface == ev->surface && !ev->surface->is_mapped())
        {
            cursor->end_held_grab();
        } else
        {
            update_cursor_position(get_current_time(), false);
        }

        if (our_touch)
        {
            if (ev && our_touch->grabbed_surface == ev->surface && !ev->surface->is_mapped())
                our_touch->end_touch_down_grab();

            for (auto f : our_touch->gesture_recognizer.current)
                handle_touch_motion(get_current_time(), f.first, f.second.sx, f.second.sy);
        }
    };
    wf::get_core().connect_signal("_surface_mapped", &surface_map_state_changed);
    wf::get_core().connect_signal("_surface_unmapped", &surface_map_state_changed);

    config_updated = [=] (wf::signal_data_t *)
    {
        for (auto& dev : input_devices)
            dev->update_options();
        for (auto& kbd : keyboards)
            kbd->reload_input_options();
    };

    wf::get_core().connect_signal("reload-config", &config_updated);

    /*

    session_listener.notify = session_signal_handler;
    wl_signal_add(&wf::get_core().ec->session_signal, &session_listener);
    */
}

input_manager::~input_manager()
{
    wf::get_core().disconnect_signal("reload-config", &config_updated);
}

uint32_t input_manager::get_modifiers()
{
    uint32_t mods = 0;
    auto keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard)
        mods = wlr_keyboard_get_modifiers(keyboard);

    return mods;
}

bool input_manager::grab_input(wayfire_grab_interface iface)
{
    if (!iface || !iface->grabbed || !session_active)
        return false;

    assert(!active_grab); // cannot have two active input grabs!

    if (our_touch)
        our_touch->input_grabbed();

    active_grab = iface;

    auto kbd = wlr_seat_get_keyboard(seat);
    auto mods = kbd->modifiers;
    mods.depressed = 0;
    wlr_seat_keyboard_send_modifiers(seat, &mods);

    set_keyboard_focus(NULL, seat);
    update_cursor_focus(nullptr, 0, 0);
    wf::get_core().set_cursor("default");
    return true;
}

void input_manager::ungrab_input()
{
    if (active_grab)
        active_grab->output->set_active_view(active_grab->output->get_active_view());
    active_grab = nullptr;

    /* We must update cursor focus, however, if we update "too soon", the current
     * pointer event (button press/release, maybe something else) will be sent to
     * the client, which shouldn't happen (at the time of the event, there was
     * still an active input grab) */
    idle_update_cursor.run_once([&] () {
        update_cursor_position(get_current_time(), false);
    });
}

bool input_manager::input_grabbed()
{
    return active_grab || !session_active;
}

void input_manager::toggle_session()
{

    session_active ^= 1;
    if (!session_active)
    {
        if (active_grab)
        {
            auto grab = active_grab;
            ungrab_input();
            active_grab = grab;
        }
    } else
    {
        if (active_grab)
        {
            auto grab = active_grab;
            active_grab = nullptr;
            grab_input(grab);
        }
    }

}

bool input_manager::can_focus_surface(wf::surface_interface_t *surface)
{
    if (exclusive_client && surface->get_client() != exclusive_client)
        return false;

    return true;
}

wf::surface_interface_t* input_manager::input_surface_at(int x, int y,
    int& lx, int& ly)
{
    auto output = wf::get_core().output_layout->get_output_coords_at(x, y, x, y);
    /* If the output at these coordinates was just destroyed or some other edge case */
    if (!output)
        return nullptr;

    auto og = output->get_layout_geometry();
    x -= og.x;
    y -= og.y;

    for (auto& view : output->workspace->get_views_in_layer(wf::VISIBLE_LAYERS))
    {
        if (can_focus_surface(view.get()))
        {
            auto surface = view->map_input_coordinates(x, y, lx, ly);
            if (surface)
                return surface;
        }
    }

    return nullptr;
}

void input_manager::set_exclusive_focus(wl_client *client)
{
    exclusive_client = client;
    for (auto& wo : wf::get_core().output_layout->get_outputs())
    {
        if (client)
            inhibit_output(wo);
        else
            uninhibit_output(wo);
    }

    /* We no longer have an exclusively focused client, so we should restore
     * focus to the topmost view */
    if (!client)
        wf::get_core().get_active_output()->refocus(nullptr);
}

/* add/remove bindings */

wf_binding* input_manager::new_binding(wf_binding_type type, wf_option value,
    wf::output_t *output, void *callback)
{
    auto binding = std::make_unique<wf_binding>();

    assert(value && output && callback);

    binding->type = type;
    binding->value = value;
    binding->output = output;
    binding->call.raw = callback;

    auto raw = binding.get();
    bindings[type].push_back(std::move(binding));

    return raw;
}

void input_manager::rem_binding(binding_criteria criteria)
{
    for(auto& category : bindings)
    {
        auto& container = category.second;
        auto it = container.begin();
        while (it != container.end())
        {
            if (criteria((*it).get())) {
                it = container.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void input_manager::rem_binding(wf_binding *binding)
{
    rem_binding([=] (wf_binding *ptr) { return binding == ptr; });
}

void input_manager::rem_binding(void *callback)
{
    rem_binding([=] (wf_binding* ptr) {return ptr->call.raw == callback; });
}

void input_manager::free_output_bindings(wf::output_t *output)
{
    rem_binding([=] (wf_binding* binding) {
        return binding->output == output;
    });
}
