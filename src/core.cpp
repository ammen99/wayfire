#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <cassert>
#include <time.h>
#include <algorithm>
#include <libinput.h>
#include <linux/input-event-codes.h>

#include <iostream>

extern "C"
{
#include <wlr/backend/libinput.h>
#include <wlr/backend/session.h>
#include <wlr/backend/multi.h>
}

#include "core.hpp"
#include "output.hpp"
#include "view.hpp"
#include "input-manager.hpp"
#include "workspace-manager.hpp"
#include "debug.hpp"
#include "render-manager.hpp"
#include "desktop-api.hpp"

#ifdef BUILD_WITH_IMAGEIO
#include "img.hpp"
#endif

#include "signal-definitions.hpp"
#include "../shared/config.hpp"
#include "wayfire-shell-protocol.h"


/* Start input_manager */

namespace {
bool grab_start_finalized;
};

/*
 TODO: probably should be made better, this is just basic gesture recognition 
struct wf_gesture_recognizer {

    constexpr static int MIN_FINGERS = 3;
    constexpr static int MIN_SWIPE_DISTANCE = 100;
    constexpr static float MIN_PINCH_DISTANCE = 70;
    constexpr static int EDGE_SWIPE_THRESHOLD = 50;

    struct finger {
        int id;
        int sx, sy;
        int ix, iy;
        bool sent_to_client, sent_to_grab;
    };

    std::map<int, finger> current;

    wlr_seat *seat;

    bool in_gesture = false, gesture_emitted = false;
    bool in_grab = false;

    int start_sum_dist;

    std::function<void(wayfire_touch_gesture)> handler;

    wf_gesture_recognizer(wlr_seat *_seat,
            std::function<void(wayfire_touch_gesture)> hnd)
    {
        seat = _seat;
        handler = hnd;
    }

    void reset_gesture()
    {
        gesture_emitted = false;

        int cx = 0, cy = 0;
        for (auto f : current) {
            cx += f.second.sx;
            cy += f.second.sy;
        }

        cx /= current.size();
        cy /= current.size();

        start_sum_dist = 0;
        for (auto &f : current) {
            start_sum_dist += std::sqrt((cx - f.second.sx) * (cx - f.second.sx)
                    + (cy - f.second.sy) * (cy - f.second.sy));

            f.second.ix = f.second.sx;
            f.second.iy = f.second.sy;
        }
    }

    void start_new_gesture(int reason_id)
    {
        in_gesture = true;
        reset_gesture();

        for (auto &f : current) {
            if (f.first != reason_id) {
                if (f.second.sent_to_client) {
                    auto t = get_ctime();
                    wlr_seat_touch_notify_up(seat, 0, f.first);
                } else if (f.second.sent_to_grab) {
                    core->input->grab_send_touch_up(touch, f.first);
                }
            }

            f.second.sent_to_grab = f.second.sent_to_client = false;
        }
    }

    void stop_gesture()
    {
        in_gesture = gesture_emitted = false;
    }

    void continue_gesture(int id, int sx, int sy)
    {
        if (gesture_emitted)
            return;

         first case - consider swipe, we go through each
         * of the directions and check whether such swipe has occured 

        bool is_left_swipe = true, is_right_swipe = true,
             is_up_swipe = true, is_down_swipe = true;

        for (auto f : current) {
            int dx = f.second.sx - f.second.ix;
            int dy = f.second.sy - f.second.iy;

            if (-MIN_SWIPE_DISTANCE < dx)
                is_left_swipe = false;
            if (dx < MIN_SWIPE_DISTANCE)
                is_right_swipe = false;

            if (-MIN_SWIPE_DISTANCE < dy)
                is_up_swipe = false;
            if (dy < MIN_SWIPE_DISTANCE)
                is_down_swipe = false;
        }

        uint32_t swipe_dir = 0;
        if (is_left_swipe)
            swipe_dir |= GESTURE_DIRECTION_LEFT;
        if (is_right_swipe)
            swipe_dir |= GESTURE_DIRECTION_RIGHT;
        if (is_up_swipe)
            swipe_dir |= GESTURE_DIRECTION_UP;
        if (is_down_swipe)
            swipe_dir |= GESTURE_DIRECTION_DOWN;

        if (swipe_dir) {
            wayfire_touch_gesture gesture;
            gesture.type = GESTURE_SWIPE;
            gesture.finger_count = current.size();
            gesture.direction = swipe_dir;

            bool bottom_edge = false, upper_edge = false,
                 left_edge = false, right_edge = false;

            auto og = core->get_active_output()->get_full_geometry();

            for (auto f : current)
            {
                bottom_edge |= (f.second.iy >= og.y + og.height - EDGE_SWIPE_THRESHOLD);
                upper_edge  |= (f.second.iy <= og.y + EDGE_SWIPE_THRESHOLD);
                left_edge   |= (f.second.ix <= og.x + EDGE_SWIPE_THRESHOLD);
                right_edge  |= (f.second.ix >= og.x + og.width - EDGE_SWIPE_THRESHOLD);
            }

            uint32_t edge_swipe_dir = 0;
            if (bottom_edge)
                edge_swipe_dir |= GESTURE_DIRECTION_UP;
            if (upper_edge)
                edge_swipe_dir |= GESTURE_DIRECTION_DOWN;
            if (left_edge)
                edge_swipe_dir |= GESTURE_DIRECTION_RIGHT;
            if (right_edge)
                edge_swipe_dir |= GESTURE_DIRECTION_LEFT;

            if ((edge_swipe_dir & swipe_dir) == swipe_dir)
                gesture.type = GESTURE_EDGE_SWIPE;

            handler(gesture);
            gesture_emitted = true;
            return;
        }

        second case - this has been a pinch 

        int cx = 0, cy = 0;
        for (auto f : current) {
            cx += f.second.sx;
            cy += f.second.sy;
        }

        cx /= current.size();
        cy /= current.size();

        int sum_dist = 0;
        for (auto f : current) {
            sum_dist += std::sqrt((cx - f.second.sx) * (cx - f.second.sx)
                    + (cy - f.second.sy) * (cy - f.second.sy));
        }

        bool inward_pinch  = (start_sum_dist - sum_dist >= MIN_PINCH_DISTANCE);
        bool outward_pinch = (start_sum_dist - sum_dist <= -MIN_PINCH_DISTANCE);

        if (inward_pinch || outward_pinch) {
            wayfire_touch_gesture gesture;
            gesture.type = GESTURE_PINCH;
            gesture.finger_count = current.size();
            gesture.direction =
                (inward_pinch ? GESTURE_DIRECTION_IN : GESTURE_DIRECTION_OUT);

            handler(gesture);
            gesture_emitted = true;
        }
    }

    void update_touch(int id, int sx, int sy)
    {
        current[id].sx = sx;
        current[id].sy = sy;

        if (in_gesture)
            continue_gesture(id, sx, sy);
    }

    timespec get_ctime()
    {
        timespec ts;
        timespec_get(&ts, TIME_UTC);

        return ts;
    }

    void register_touch(int id, int sx, int sy)
    {
        debug << "register touch " << id << std::endl;
        auto& f = current[id] = {id, sx, sy, sx, sy, false, false};
        if (in_gesture)
            reset_gesture();

        if (current.size() >= MIN_FINGERS && !in_gesture)
            start_new_gesture(id);

        bool send_to_client = !in_gesture && !in_grab;
        bool send_to_grab = !in_gesture && in_grab;

        if (send_to_client && id < 1)
        {
            core->input->check_touch_bindings(touch,
                    wl_fixed_from_int(sx), wl_fixed_from_int(sy));
        }

        while checking for touch grabs, some plugin might have started the grab,
         * so check again 
        if (in_grab && send_to_client)
        {
            send_to_client = false;
            send_to_grab = true;
        }

        f.sent_to_grab = send_to_grab;
        f.sent_to_client = send_to_client;

        assert(!send_to_grab || !send_to_client);

        if (send_to_client)
        {
            timespec t = get_ctime();
            weston_touch_send_down(touch, &t, id, wl_fixed_from_int(sx),
                    wl_fixed_from_int(sy));
        } else if (send_to_grab)
        {
            core->input->grab_send_touch_down(touch, id, wl_fixed_from_int(sx),
                    wl_fixed_from_int(sy));
        }
    }

    void unregister_touch(int id)
    {
        shouldn't happen, but just in case 
        if (!current.count(id))
            return;

        debug << "unregister touch\n";

        finger f = current[id];
        current.erase(id);
        if (in_gesture) {
            if (current.size() < MIN_FINGERS) {
                stop_gesture();
            } else {
                reset_gesture();
            }
        } else if (f.sent_to_client) {
            timespec t = get_ctime();
            weston_touch_send_up(touch, &t, id);
        } else if (f.sent_to_grab) {
            core->input->grab_send_touch_up(touch, id);
        }
    }

    bool is_finger_sent_to_client(int id)
    {
        auto it = current.find(id);
        if (it == current.end())
            return false;
        return it->second.sent_to_client;
    }

    bool is_finger_sent_to_grab(int id)
    {
        auto it = current.find(id);
        if (it == current.end())
            return false;
        return it->second.sent_to_grab;
    }

    void start_grab()
    {
        in_grab = true;

        for (auto &f : current)
        {
            if (f.second.sent_to_client)
            {
                timespec t = get_ctime();
                weston_touch_send_up(touch, &t, f.first);
            }

            f.second.sent_to_client = false;

            if (!in_gesture)
            {
                core->input->grab_send_touch_down(touch, f.first,
                        wl_fixed_from_int(f.second.sx), wl_fixed_from_int(f.second.sy));
                f.second.sent_to_grab = true;
            }
        }
    }

    void end_grab()
    {
        in_grab = false;
    }
}; */

/* these simply call the corresponding input_manager functions,
 * you can think of them as wrappers for use of libweston
void touch_grab_down(weston_touch_grab *grab, const timespec* time, int id,
        wl_fixed_t sx, wl_fixed_t sy)
{
    core->input->propagate_touch_down(grab->touch, time, id, sx, sy);
}

void touch_grab_up(weston_touch_grab *grab, const timespec* time, int id)
{
    core->input->propagate_touch_up(grab->touch, time, id);
}

void touch_grab_motion(weston_touch_grab *grab, const timespec* time, int id,
        wl_fixed_t sx, wl_fixed_t sy)
{
    core->input->propagate_touch_motion(grab->touch, time, id, sx, sy);
}

void touch_grab_frame(weston_touch_grab*) {}
void touch_grab_cancel(weston_touch_grab*) {}

static const weston_touch_grab_interface touch_grab_interface = {
    touch_grab_down,  touch_grab_up, touch_grab_motion,
    touch_grab_frame, touch_grab_cancel
};

 called upon the corresponding event, we actually just call the gesture
 * recognizer functions, they will send the touch event to the client
 * or to plugin callbacks, or emit a gesture 
void input_manager::propagate_touch_down(weston_touch* touch, const timespec* time,
        int32_t id, wl_fixed_t sx, wl_fixed_t sy)
{
    gr->touch = touch;
    gr->register_touch(id, wl_fixed_to_int(sx), wl_fixed_to_int(sy));
}

void input_manager::propagate_touch_up(weston_touch* touch, const timespec* time,
        int32_t id)
{
    gr->touch = touch;
    gr->unregister_touch(id);
}

void input_manager::propagate_touch_motion(weston_touch* touch, const timespec* time,
        int32_t id, wl_fixed_t sx, wl_fixed_t sy)
{
    gr->touch = touch;
    gr->update_touch(id, wl_fixed_to_int(sx), wl_fixed_to_int(sy));

    if (gr->is_finger_sent_to_client(id)) {
        weston_touch_send_motion(touch, time, id, sx, sy);
    } else if(gr->is_finger_sent_to_grab(id)) {
        grab_send_touch_motion(touch, id, sx, sy);
    }
}


 grab_send_touch_down/up/motion() are called from the gesture recognizer
 * in case they should be processed by plugin grabs 
void input_manager::grab_send_touch_down(weston_touch* touch, int32_t id,
        wl_fixed_t sx, wl_fixed_t sy)
{
    if (active_grab && active_grab->callbacks.touch.down)
        active_grab->callbacks.touch.down(touch, id, sx, sy);
}

void input_manager::grab_send_touch_up(weston_touch* touch, int32_t id)
{
    if (active_grab && active_grab->callbacks.touch.up)
        active_grab->callbacks.touch.up(touch, id);
}

void input_manager::grab_send_touch_motion(weston_touch* touch, int32_t id,
        wl_fixed_t sx, wl_fixed_t sy)
{
    if (active_grab && active_grab->callbacks.touch.motion)
        active_grab->callbacks.touch.motion(touch, id, sx, sy);
}

void input_manager::check_touch_bindings(weston_touch* touch, wl_fixed_t sx, wl_fixed_t sy)
{
    uint32_t mods = tgrab.touch->seat->modifier_state;
    std::vector<touch_callback*> calls;
    for (auto listener : touch_listeners) {
        if (listener.second.mod == mods &&
                listener.second.output == core->get_active_output()) {
            calls.push_back(listener.second.call);
        }
    }

    for (auto call : calls)
        (*call)(touch, sx, sy);
}
*/

/* TODO: reorganize input-manager code, perhaps move it to another file */
struct wf_callback
{
    int id;
    wayfire_output *output;
    uint32_t mod;
};

struct key_callback_data : wf_callback
{
    key_callback *call;
    uint32_t key;
};

struct button_callback_data : wf_callback
{
    button_callback *call;
    uint32_t button;
};

/* TODO: inhibit idle */
static void handle_pointer_button_cb(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_event_pointer_button*> (data);
    core->input->handle_pointer_button(ev);
        wlr_seat_pointer_notify_button(core->input->seat, ev->time_msec,
                                       ev->button, ev->state);
}

static void handle_pointer_motion_cb(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_event_pointer_motion*> (data);
    core->input->handle_pointer_motion(ev);
}

static void handle_pointer_motion_absolute_cb(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_event_pointer_motion_absolute*> (data);
    core->input->handle_pointer_motion_absolute(ev);
}

static void handle_pointer_axis_cb(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_event_pointer_axis*> (data);
    core->input->handle_pointer_axis(ev);
}

static void handle_keyboard_key_cb(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_event_keyboard_key*> (data);
    if (!core->input->handle_keyboard_key(ev->keycode, ev->state))
    {
        wlr_seat_keyboard_notify_key(core->input->seat, ev->time_msec,
                                     ev->keycode, ev->state);
    }
}

static uint32_t mod_from_key(uint32_t key)
{
    if (key == KEY_LEFTALT || key == KEY_RIGHTALT)
        return WLR_MODIFIER_ALT;
    if (key == KEY_LEFTCTRL || key == KEY_RIGHTCTRL)
        return WLR_MODIFIER_CTRL;
    if (key == KEY_LEFTSHIFT || key == KEY_RIGHTSHIFT)
        return WLR_MODIFIER_SHIFT;
    if (key == KEY_LEFTMETA || key == KEY_RIGHTMETA)
        return WLR_MODIFIER_LOGO;

    return 0;
}

static void handle_keyboard_mod_cb(wl_listener*, void* data)
{
    auto kbd = static_cast<wlr_keyboard*> (data);
    if (!core->input->input_grabbed())
        wlr_seat_keyboard_send_modifiers(core->input->seat, &kbd->modifiers);
}

static void handle_request_set_cursor(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_seat_pointer_request_set_cursor_event*> (data);
    core->input->set_cursor(ev);
}

static bool check_vt_switch(wlr_session *session, uint32_t key, uint32_t mods)
{
    if (!session)
        return false;
    if (mods ^ (WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL))
        return false;

    if (key < KEY_F1 || key > KEY_F10)
        return false;

    int target_vt = key - KEY_F1 + 1;
    wlr_session_change_vt(session, target_vt);
    return true;
}

bool input_manager::handle_keyboard_key(uint32_t key, uint32_t state)
{

    auto mod = mod_from_key(key);
    if (mod && handle_keyboard_mod(mod, state))
        return true;

    if (active_grab && active_grab->callbacks.keyboard.key)
        active_grab->callbacks.keyboard.key(key, state);

    if (state == WLR_KEY_PRESSED)
    {

        if (check_vt_switch(wlr_multi_get_session(core->backend), key, get_modifiers()))
            return true;

        std::vector<key_callback*> callbacks;

        auto mod_state = get_modifiers();

        for (auto& pair : key_bindings)
        {
            auto& binding = pair.second;
            if (binding->output == core->get_active_output() &&
                mod_state == binding->mod && key == binding->key)
                callbacks.push_back(binding->call);
        }

        for (auto call : callbacks)
            (*call) (key);

        if (callbacks.size())
            return true;
    }

    return active_grab;
}

bool input_manager::handle_keyboard_mod(uint32_t modifier, uint32_t state)
{
    mods_count[modifier] += (state == WLR_KEY_PRESSED ? 1 : -1);
    if (active_grab)
    {
        if (active_grab->callbacks.keyboard.mod)
            active_grab->callbacks.keyboard.mod(modifier, state);
        return true;
    }

    return false;
}

void input_manager::handle_pointer_button(wlr_event_pointer_button *ev)
{
    /* TODO: do we need this?
    if (ev->state == WLR_BUTTON_RELEASED)
    {
        cursor_focus = nullptr;
        update_cursor_position(ev->time_msec, false);
    } */

    if (ev->state == WLR_BUTTON_PRESSED)
    {
        std::vector<button_callback*> callbacks;

        auto mod_state = get_modifiers();
        for (auto& pair : button_bindings)
        {
            auto& binding = pair.second;
            if (binding->output == core->get_active_output() &&
                mod_state == binding->mod && ev->button == binding->button)
                callbacks.push_back(binding->call);
        }

        for (auto call : callbacks)
            (*call) (ev->button, cursor->x, cursor->y);
    }

    if (active_grab && active_grab->callbacks.pointer.button)
        active_grab->callbacks.pointer.button(ev->button, ev->state);
}

void input_manager::update_cursor_focus(wayfire_surface_t *focus, int x, int y)
{
    cursor_focus = focus;
    if (focus)
    {
        wlr_seat_pointer_notify_enter(seat, focus->surface, x, y);
    } else
    {
        wlr_seat_pointer_clear_focus(seat);
        core->set_default_cursor();
    }
}

void input_manager::update_cursor_position(uint32_t time_msec, bool real_update)
{
    auto output = core->get_output_at(cursor->x, cursor->y);
    assert(output);

    if (input_grabbed() && real_update)
    {
        if (active_grab->callbacks.pointer.motion)
            active_grab->callbacks.pointer.motion(cursor->x, cursor->y);
        return;
    }

    int sx = cursor->x, sy = cursor->y;
    wayfire_surface_t *new_focus = NULL;

    output->workspace->for_all_view(
        [&] (wayfire_view view)
        {
            if (new_focus) return;
            new_focus = view->map_input_coordinates(cursor->x, cursor->y, sx, sy);
        });

    update_cursor_focus(new_focus, sx, sy);
    wlr_seat_pointer_notify_motion(core->input->seat, time_msec, sx, sy);
}

void input_manager::handle_pointer_motion(wlr_event_pointer_motion *ev)
{
    wlr_cursor_move(cursor, ev->device, ev->delta_x, ev->delta_y);
    update_cursor_position(ev->time_msec);
}

void input_manager::handle_pointer_motion_absolute(wlr_event_pointer_motion_absolute *ev)
{
    wlr_cursor_warp_absolute(cursor, ev->device, ev->x, ev->y);
    update_cursor_position(ev->time_msec);;
}

void input_manager::handle_pointer_axis(wlr_event_pointer_axis *ev)
{
    if (active_grab)
    {
        if (active_grab->callbacks.pointer.axis)
            active_grab->callbacks.pointer.axis(ev);

        return;
    }

    wlr_seat_pointer_notify_axis(seat, ev->time_msec, ev->orientation, ev->delta);
}

void input_manager::set_cursor(wlr_seat_pointer_request_set_cursor_event *ev)
{
    if (ev->surface && ev->seat_client->seat->pointer_state.focused_client == ev->seat_client && !input_grabbed())
        wlr_cursor_set_surface(cursor, ev->surface, ev->hotspot_x, ev->hotspot_y);
    else
        core->set_default_cursor();
}

bool input_manager::is_touch_enabled()
{
    return touch_count > 0;
}

/* TODO: possibly add more input options which aren't available right now */
namespace device_config
{
    bool touchpad_tap_enabled;
    bool touchpad_dwl_enabled;
    bool touchpad_natural_scroll_enabled;

    std::string drm_device;

    wayfire_config *config;

    void load(wayfire_config *conf)
    {
        config = conf;

        auto section = config->get_section("input");
        touchpad_tap_enabled = section->get_int("tap_to_click", 1);
        touchpad_dwl_enabled = section->get_int("disable_while_typing", 1);
        touchpad_natural_scroll_enabled = section->get_int("natural_scroll", 0);

        drm_device = config->get_section("core")->get_string("drm_device", "default");
    }
}

void configure_input_device(libinput_device *device)
{
    assert(device);
    /* we are configuring a touchpad */
    if (libinput_device_config_tap_get_finger_count(device) > 0)
    {
        libinput_device_config_tap_set_enabled(device,
                device_config::touchpad_tap_enabled ?
                    LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
        libinput_device_config_dwt_set_enabled(device,
                device_config::touchpad_dwl_enabled ?
                LIBINPUT_CONFIG_DWT_ENABLED : LIBINPUT_CONFIG_DWT_DISABLED);

        if (libinput_device_config_scroll_has_natural_scroll(device) > 0)
        {
            libinput_device_config_scroll_set_natural_scroll_enabled(device,
                    device_config::touchpad_natural_scroll_enabled);
        }
    }
}

void input_manager::update_capabilities()
{
    uint32_t cap = 0;
    if (pointer_count)
        cap |= WL_SEAT_CAPABILITY_POINTER;
    if (keyboard_count)
        cap |= WL_SEAT_CAPABILITY_KEYBOARD;
    if (touch_count)
        cap |= WL_SEAT_CAPABILITY_TOUCH;

    wlr_seat_set_capabilities(seat, cap);
}

void handle_new_input_cb(wl_listener*, void *data)
{
    auto dev = static_cast<wlr_input_device*> (data);
    assert(dev);
    core->input->handle_new_input(dev);
}

/* TODO: repeat info, xkb options - language, model, etc */
void input_manager::setup_keyboard(wlr_input_device *dev)
{
    auto ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_rule_names rules = {0, 0, 0, 0, 0};
    auto keymap = xkb_map_new_from_names(ctx, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);

    wlr_keyboard_set_keymap(dev->keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);

    wlr_keyboard_set_repeat_info(dev->keyboard, 40, 400);

    wl_signal_add(&dev->keyboard->events.key, &key);
    wl_signal_add(&dev->keyboard->events.modifiers, &modifier);
    wlr_seat_set_keyboard(seat, dev);

    keyboard_count++;
}

void input_manager::handle_new_input(wlr_input_device *dev)
{
    if (!cursor)
        create_seat();

    log_info("add new input: %s", dev->name);
    if (dev->type == WLR_INPUT_DEVICE_KEYBOARD)
        setup_keyboard(dev);

    if (dev->type == WLR_INPUT_DEVICE_POINTER)
    {
        wlr_cursor_attach_input_device(cursor, dev);
        pointer_count++;
    }

    if (dev->type == WLR_INPUT_DEVICE_TOUCH)
        touch_count++;

    if (wlr_input_device_is_libinput(dev))
        configure_input_device(wlr_libinput_get_device_handle(dev));

    update_capabilities();
}

void input_manager::create_seat()
{
    cursor = wlr_cursor_create();

    wlr_cursor_attach_output_layout(cursor, core->output_layout);
    wlr_cursor_map_to_output(cursor, NULL);
    wlr_cursor_warp(cursor, NULL, cursor->x, cursor->y);

    xcursor = wlr_xcursor_manager_create(NULL, 32);
    wlr_xcursor_manager_load(xcursor, 1);

    core->set_default_cursor();

    wl_signal_add(&cursor->events.button, &button);
    wl_signal_add(&cursor->events.motion, &motion);
    wl_signal_add(&cursor->events.motion_absolute, &motion_absolute);
    wl_signal_add(&cursor->events.axis, &axis);
    wl_signal_add(&seat->events.request_set_cursor, &request_set_cursor);
}

input_manager::input_manager()
{
    input_device_created.notify = handle_new_input_cb;
    seat = wlr_seat_create(core->display, "default");
    wl_signal_add(&core->backend->events.new_input,
                  &input_device_created);

    key.notify                = handle_keyboard_key_cb;
    modifier.notify           = handle_keyboard_mod_cb;
    button.notify             = handle_pointer_button_cb;
    motion.notify             = handle_pointer_motion_cb;
    motion_absolute.notify    = handle_pointer_motion_absolute_cb;
    axis.notify               = handle_pointer_axis_cb;
    request_set_cursor.notify = handle_request_set_cursor;

    /*
    if (is_touch_enabled())
    {

        auto touch = weston_seat_get_touch(core->get_current_seat());
        tgrab.interface = &touch_grab_interface;
        tgrab.touch = touch;
        weston_touch_start_grab(touch, &tgrab);

        using namespace std::placeholders;
        gr = new wf_gesture_recognizer(touch,
                                       std::bind(std::mem_fn(&input_manager::handle_gesture),
                                                 this, _1));
    }

    session_listener.notify = session_signal_handler;
    wl_signal_add(&core->ec->session_signal, &session_listener);
    */
}

uint32_t input_manager::get_modifiers()
{
    auto kbd = wlr_seat_get_keyboard(seat);
    if (!kbd)
        return 0;

    return wlr_keyboard_get_modifiers(kbd);
}

// TODO: set pointer, reset mods, grab gr */
bool input_manager::grab_input(wayfire_grab_interface iface)
{
    if (!iface || !iface->grabbed || !session_active)
        return false;

    assert(!active_grab); // cannot have two active input grabs!
    active_grab = iface;
    update_cursor_focus(nullptr, 0, 0);

    /*
    if (ptr)
    {
        weston_pointer_start_grab(ptr, &pgrab);
        auto background = core->get_active_output()->workspace->get_background_view();
        if (background)
        {
            weston_pointer_clear_focus(ptr);
            weston_pointer_set_focus(ptr, background->handle, -10000000, -1000000);
        }
    }

    grab_start_finalized = false;


    if (is_touch_enabled())
        gr->start_grab();
        */

    return true;
}

void input_manager::ungrab_input()
{
    active_grab = nullptr;

    /*

        weston_keyboard_send_modifiers(kbd,
                                       wl_display_next_serial(core->ec->wl_display),
                                       0,
                                       kbd->modifiers.mods_latched,
                                       kbd->modifiers.mods_locked,
                                       kbd->modifiers.group);

    if (is_touch_enabled())
        gr->end_grab();
        */
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
static int _last_id = 0;

int input_manager::add_key(uint32_t mod, uint32_t key,
                                key_callback *call, wayfire_output *output)
{
    auto kcd = new key_callback_data;
    kcd->call = call;
    kcd->output = output;
    kcd->mod = mod;
    kcd->key = key;
    kcd->id = ++_last_id;

    key_bindings[_last_id] = kcd;
    return _last_id;
}

void input_manager::rem_key(int id)
{
    auto it = key_bindings.find(id);
    if (it != key_bindings.end())
    {
        delete it->second;
        key_bindings.erase(it);
    }
}

void input_manager::rem_key(key_callback *cb)
{
    auto it = key_bindings.begin();

    while(it != key_bindings.end())
    {
        if (it->second->call == cb)
        {
            delete it->second;
            it = key_bindings.erase(it);
        } else
            ++it;
    }
}

int input_manager::add_button(uint32_t mod, uint32_t button,
                                button_callback *call, wayfire_output *output)
{
    auto bcd = new button_callback_data;
    bcd->call = call;
    bcd->output = output;
    bcd->mod = mod;
    bcd->button = button;
    bcd->id = ++_last_id;

    button_bindings[_last_id] = bcd;
    return _last_id;
}

void input_manager::rem_button(int id)
{
    auto it = button_bindings.find(id);
    if (it != button_bindings.end())
    {
        delete it->second;
        button_bindings.erase(it);
    }
}

void input_manager::rem_button(button_callback *cb)
{
    auto it = button_bindings.begin();

    while(it != button_bindings.end())
    {
        if (it->second->call == cb)
        {
            delete it->second;
            it = button_bindings.erase(it);
        } else
            ++it;
    }
}

int input_manager::add_touch(uint32_t mods, touch_callback* call, wayfire_output *output)
{
    int sz = 0;
    if (!touch_listeners.empty())
        sz = (--touch_listeners.end())->first + 1;

    touch_listeners[sz] = {mods, call, output};
    return sz;
}

void input_manager::rem_touch(int id)
{
    touch_listeners.erase(id);
}

void input_manager::rem_touch(touch_callback *tc)
{
    std::vector<int> ids;
    for (const auto& x : touch_listeners)
        if (x.second.call == tc)
            ids.push_back(x.first);

    for (auto x : ids)
        rem_touch(x);
}

int input_manager::add_gesture(const wayfire_touch_gesture& gesture,
        touch_gesture_callback *callback, wayfire_output *output)
{
    gesture_listeners[gesture_id] = {gesture, callback, output};
    gesture_id++;
    return gesture_id - 1;
}

void input_manager::rem_gesture(int id)
{
    gesture_listeners.erase(id);
}

void input_manager::rem_gesture(touch_gesture_callback *cb)
{
    std::vector<int> ids;
    for (const auto& x : gesture_listeners)
        if (x.second.call == cb)
            ids.push_back(x.first);

    for (auto x : ids)
        rem_gesture(x);
}

void input_manager::free_output_bindings(wayfire_output *output)
{
    std::vector<int> bindings;
    for (auto kcd : key_bindings)
        if (kcd.second->output == output)
            bindings.push_back(kcd.second->id);

    for (auto x : bindings)
        rem_key(x);

    bindings.clear();
    for (auto bcd : button_bindings)
        if (bcd.second->output == output)
            bindings.push_back(bcd.second->id);

    for (auto x : bindings)
        rem_button(x);

    std::vector<int> ids;
    for (const auto& x : touch_listeners)
        if (x.second.output == output)
            ids.push_back(x.first);
    for (auto x : ids)
        rem_touch(x);

    ids.clear();
    for (const auto& x : gesture_listeners)
        if (x.second.output == output)
            ids.push_back(x.first);
    for (auto x : ids)
        rem_gesture(x);
}

void input_manager::handle_gesture(wayfire_touch_gesture g)
{
    for (const auto& listener : gesture_listeners) {
        if (listener.second.gesture.type == g.type &&
            listener.second.gesture.finger_count == g.finger_count &&
            core->get_active_output() == listener.second.output)
        {
            (*listener.second.call)(&g);
        }
    }
}

/* End input_manager */

void wayfire_core::configure(wayfire_config *config)
{
    this->config = config;
    auto section = config->get_section("core");

    vwidth  = section->get_int("vwidth", 3);
    vheight = section->get_int("vheight", 3);

    shadersrc   = section->get_string("shadersrc", INSTALL_PREFIX "/share/wayfire/shaders");
    plugin_path = section->get_string("plugin_path_prefix", INSTALL_PREFIX "/lib/");
    plugins     = section->get_string("plugins", "viewport_impl move resize animation switcher vswitch cube expo command grid");
    run_panel   = section->get_int("run_panel", 1);

    section = config->get_section("input");

    std::string model   = section->get_string("xkb_model", "pc100");
    std::string variant = section->get_string("xkb_variant", "");
    std::string layout  = section->get_string("xkb_layout", "us");
    std::string options = section->get_string("xkb_option", "");
    std::string rules   = section->get_string("xkb_rule", "evdev");

    // TODO: set keyboard options
    xkb_rule_names names;
    names.rules   = strdup(rules.c_str());
    names.model   = strdup(model.c_str());
    names.layout  = strdup(layout.c_str());
    names.variant = strdup(variant.c_str());
    names.options = strdup(options.c_str());

    //weston_compositor_set_xkb_rule_names(ec, &names);

    //ec->kb_repeat_rate  = section->get_int("kb_repeat_rate", 40);
    //ec->kb_repeat_delay = section->get_int("kb_repeat_delay", 400);
}

void finish_wf_shell_bind_cb(void *data)
{
    auto resource = (wl_resource*) data;
    core->shell_clients.push_back(resource);
    core->for_each_output([=] (wayfire_output *out) {
        GetTuple(sw, sh, out->get_screen_size());
        wayfire_shell_send_output_created(resource,
                                          out->id, sw, sh);
        /*
        if (out->handle->set_gamma) {
            wayfire_shell_send_gamma_size(resource,
                    out->handle->id, out->handle->gamma_size);
        } */
    });
}

void unbind_desktop_shell(wl_resource *resource)
{
    auto it = std::find(core->shell_clients.begin(), core->shell_clients.end(),
                        resource);
    core->shell_clients.erase(it);
}

void bind_desktop_shell(wl_client *client, void *data, uint32_t version, uint32_t id)
{
    auto resource = wl_resource_create(client, &wayfire_shell_interface, 1, id);
    wl_resource_set_implementation(resource, &shell_interface_impl,
            NULL, unbind_desktop_shell);

    auto loop = wl_display_get_event_loop(core->display);
    wl_event_loop_add_idle(loop, finish_wf_shell_bind_cb, resource);
}

void wayfire_core::init(wayfire_config *conf)
{
    configure(conf);
    device_config::load(conf);

    data_device_manager = wlr_data_device_manager_create(display);
    wl_display_init_shm(core->display);

    output_layout = wlr_output_layout_create();
    core->compositor = wlr_compositor_create(display, wlr_backend_get_renderer(backend));
    init_desktop_apis();
    input = new input_manager();

#ifdef BUILD_WITH_IMAGEIO
    image_io::init();
#endif

    if (wl_global_create(display, &wayfire_shell_interface,
                         1, NULL, bind_desktop_shell) == NULL) {
        log_error("Failed to create wayfire_shell interface");
    }

}

bool wayfire_core::set_decorator(decorator_base_t *decor)
{
    if (api->decorator)
        return false;

    return (api->decorator = decor);
}

void refocus_idle_cb(void *data)
{
    core->refocus_active_output_active_view();
}

void wayfire_core::wake()
{
    if (times_wake == 0 && run_panel)
        run(INSTALL_PREFIX "/lib/wayfire/wayfire-shell-client");

    for (auto o : pending_outputs)
        add_output(o);
    pending_outputs.clear();

    auto loop = wl_display_get_event_loop(display);
    wl_event_loop_add_idle(loop, refocus_idle_cb, 0);

    if (times_wake > 0)
    {
        for_each_output([] (wayfire_output *output)
                        { output->emit_signal("wake", nullptr); });
    }

    ++times_wake;
}

void wayfire_core::sleep()
{
    for_each_output([] (wayfire_output *output)
            { output->emit_signal("sleep", nullptr); });
}

wlr_seat* wayfire_core::get_current_seat()
{ return input->seat; }

static void output_destroyed_callback(wl_listener *, void *data)
{
    core->remove_output(core->get_output((wlr_output*) data));
}

void wayfire_core::set_default_cursor()
{
    if (input->cursor)
        wlr_xcursor_manager_set_cursor_image(input->xcursor, "left_ptr", input->cursor);
}

std::tuple<int, int> wayfire_core::get_cursor_position()
{
    if (input->cursor)
        return std::tuple<int, int> (input->cursor->x, input->cursor->y);
    else
        return std::tuple<int, int> (0, 0);
}

wayfire_surface_t *wayfire_core::get_cursor_focus()
{
    return input->cursor_focus;
}

static int _last_output_id = 0;
/* TODO: remove pending_outputs, they are no longer necessary */
void wayfire_core::add_output(wlr_output *output)
{
    log_info("add new output: %s", output->name);
    if (outputs.find(output) != outputs.end())
        return;

    if (!input) {
        pending_outputs.push_back(output);
        return;
    }

    wayfire_output *wo = outputs[output] = new wayfire_output(output, config);
    wo->id = _last_output_id++;
    focus_output(wo);

    wo->destroy_listener.notify = output_destroyed_callback;
    wl_signal_add(&wo->handle->events.destroy, &wo->destroy_listener);

    for (auto resource : shell_clients)
        wayfire_shell_send_output_created(resource, wo->id,
                output->width, output->height);
}

void wayfire_core::remove_output(wayfire_output *output)
{
    log_info("removing output: %s", output->handle->name);

    outputs.erase(output->handle);
    wl_list_remove(&output->destroy_listener.link);

    /* we have no outputs, simply quit */
    if (outputs.empty())
    {
        std::exit(0);
    }

    if (output == active_output)
        focus_output(outputs.begin()->second);

 //   auto og = output->get_full_geometry();
  //  auto ng = active_output->get_full_geometry();
   // int dx = ng.x - og.x, dy = ng.y - og.y;

    /* first move each desktop view(e.g windows) to another output */
    output->workspace->for_each_view_reverse([=] (wayfire_view view)
    {
        output->workspace->view_removed(view);
        view->set_output(nullptr);

        active_output->attach_view(view);
        /* TODO: do we actually move()? */
       // view->move(view->get_geometry().x + dx, view->get_geometry().y + dy);
        active_output->focus_view(view);
    });

    /* just remove all other views - backgrounds, panels, etc.
     * desktop views have been removed by the previous cycle */
    output->workspace->for_all_view([output] (wayfire_view view)
    {
        output->workspace->view_removed(view);
        view->set_output(nullptr);
    });

    delete output;
    for (auto resource : shell_clients)
        wayfire_shell_send_output_destroyed(resource, output->id);
}

void wayfire_core::refocus_active_output_active_view()
{
    if (!active_output)
        return;

    auto view = active_output->get_top_view();
    if (view) {
        active_output->focus_view(nullptr);
        active_output->focus_view(view);
    }
}

void wayfire_core::focus_output(wayfire_output *wo)
{
    assert(wo);
    if (active_output == wo)
        return;

    wo->ensure_pointer();

    wayfire_grab_interface old_grab = nullptr;

    if (active_output)
    {
        old_grab = active_output->get_input_grab_interface();
        active_output->focus_view(nullptr);
    }

    active_output = wo;
    if (wo)
        log_debug("focus output: %s", wo->handle->name);

    /* invariant: input is grabbed only if the current output
     * has an input grab */
    if (input->input_grabbed())
    {
        assert(old_grab);
        input->ungrab_input();
    }

    wayfire_grab_interface iface = wo->get_input_grab_interface();

    /* this cannot be recursion as active_output will be equal to wo,
     * and wo->active_view->output == wo */
    if (!iface)
        refocus_active_output_active_view();
    else
        input->grab_input(iface);

    if (active_output)
    {
        wlr_output_schedule_frame(active_output->handle);
        active_output->emit_signal("output-gain-focus", nullptr);
    }
}

wayfire_output* wayfire_core::get_output(wlr_output *handle)
{
    auto it = outputs.find(handle);
    if (it != outputs.end()) {
        return it->second;
    } else {
        return nullptr;
    }
}

wayfire_output* wayfire_core::get_active_output()
{
    return active_output;
}

wayfire_output* wayfire_core::get_output_at(int x, int y)
{
    wayfire_output *target = nullptr;
    for_each_output([&] (wayfire_output *output)
    {
        if (point_inside({x, y}, output->get_full_geometry()) &&
                target == nullptr)
        {
            target = output;
        }
    });

    return target;
}

wayfire_output* wayfire_core::get_next_output(wayfire_output *output)
{
    if (outputs.empty())
        return output;
    auto id = output->handle;
    auto it = outputs.find(id);
    ++it;

    if (it == outputs.end()) {
        return outputs.begin()->second;
    } else {
        return it->second;
    }
}

size_t wayfire_core::get_num_outputs()
{
    return outputs.size();
}

void wayfire_core::for_each_output(output_callback_proc call)
{
    for (auto o : outputs)
        call(o.second);
}

void wayfire_core::add_view(wayfire_view view)
{
    views[view->surface] = view;
    assert(active_output);
}

wayfire_view wayfire_core::find_view(wlr_surface *handle)
{
    auto it = views.find(handle);
    if (it == views.end()) {
        return nullptr;
    } else {
        return it->second;
    }
}

wayfire_view wayfire_core::find_view(uint32_t id)
{
    for (auto v : views)
        if (v.second->get_id() == id)
            return v.second;

    return nullptr;
}

void wayfire_core::focus_view(wayfire_view v, wlr_seat *seat)
{
    if (!v)
        return;

    if (v->get_output() != active_output)
        focus_output(v->get_output());

    active_output->focus_view(v, seat);
}

void wayfire_core::erase_view(wayfire_view v)
{
    if (!v) return;

    /* TODO: what do we do now? */
    views.erase(v->surface);

    if (v->get_output())
        v->get_output()->detach_view(v);

    /*
    if (v->handle && destroy_handle)
        weston_view_destroy(v->handle);
        */
}

void wayfire_core::run(const char *command)
{
    pid_t pid = fork();

    /* The following is a "hack" for disowning the child processes,
     * otherwise they will simply stay as zombie processes */
    if (!pid) {
        if (!fork()) {
            setenv("WAYLAND_DISPLAY", wayland_display.c_str(), 1);
            auto xdisp = ":" + std::to_string(api->xwayland->display);
            setenv("DISPLAY", xdisp.c_str(), 1);

            exit(execl("/bin/sh", "/bin/bash", "-c", command, NULL));
        } else {
            exit(0);
        }
    } else {
        int status;
        waitpid(pid, &status, 0);
    }
}

void wayfire_core::move_view_to_output(wayfire_view v, wayfire_output *new_output)
{
    assert(new_output);
    if (v->get_output())
        v->get_output()->detach_view(v);

    new_output->attach_view(v);
}

wayfire_core *core;
