#include <unistd.h>
#include <sys/wait.h>
#include <cstring>

#include <libweston-desktop.h>

#include "core.hpp"
#include "output.hpp"
#include "input-manager.hpp"

#if BUILD_WITH_IMAGEIO
#include "img.hpp"
#endif

#include "signal_definitions.hpp"
#include "../shared/config.hpp"
#include "../proto/wayfire-shell-server.h"


/* Start input_manager */

namespace {
bool grab_start_finalized;
};

/* TODO: probably should be made better, this is just basic gesture recognition */
struct wf_gesture_recognizer {

    constexpr static int MIN_FINGERS = 3;
    constexpr static int MIN_SWIPE_DISTANCE = 100;
    constexpr static float MIN_PINCH_DISTANCE = 70;

    struct finger {
        int id;
        int sx, sy;
        int ix, iy;
        bool sent_to_client, sent_to_grab;
    };

    std::map<int, finger> current;

    uint32_t last_time;
    weston_touch *touch;

    bool in_gesture = false, gesture_emitted = false;
    bool in_grab = false;

    int start_sum_dist;

    std::function<void(wayfire_touch_gesture)> handler;

    wf_gesture_recognizer(weston_touch *_touch,
            std::function<void(wayfire_touch_gesture)> hnd)
    {
        touch = _touch;
        last_time = 0;
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
                    weston_touch_send_up(touch, last_time, f.first);
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

        /* first case - consider swipe, we go through each
         * of the directions and check whether such swipe has occured */

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


            handler(gesture);
            gesture_emitted = true;
            return;
        }

        /* second case - this has been a pinch */

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

    void register_touch(int id, int sx, int sy)
    {
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

        /* while checking for touch grabs, some plugin might have started the grab,
         * so check again */
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
            weston_touch_send_down(touch, last_time, id, wl_fixed_from_int(sx),
                    wl_fixed_from_int(sy));
        } else if (send_to_grab)
        {
            core->input->grab_send_touch_down(touch, id, wl_fixed_from_int(sx),
                    wl_fixed_from_int(sy));
        }
    }

    void unregister_touch(int id)
    {
        /* shouldn't happen, but just in case */
        if (!current.count(id))
            return;

        finger f = current[id];
        current.erase(id);
        if (in_gesture) {
            if (current.size() < MIN_FINGERS) {
                stop_gesture();
            } else {
                reset_gesture();
            }
        } else if (f.sent_to_client) {
            weston_touch_send_up(touch, last_time, id);
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
                weston_touch_send_up(touch, last_time, f.first);

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
};

/* these simply call the corresponding input_manager functions,
 * you can think of them as wrappers for use of libweston */
void touch_grab_down(weston_touch_grab *grab, uint32_t time, int id,
        wl_fixed_t sx, wl_fixed_t sy)
{
    core->input->propagate_touch_down(grab->touch, time, id, sx, sy);
}

void touch_grab_up(weston_touch_grab *grab, uint32_t time, int id)
{
    core->input->propagate_touch_up(grab->touch, time, id);
}

void touch_grab_motion(weston_touch_grab *grab, uint32_t time, int id,
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

/* called upon the corresponding event, we actually just call the gesture
 * recognizer functions, they will send the touch event to the client
 * or to plugin callbacks, or emit a gesture */
void input_manager::propagate_touch_down(weston_touch* touch, uint32_t time,
        int32_t id, wl_fixed_t sx, wl_fixed_t sy)
{
    gr->last_time = time;
    gr->touch = touch;
    gr->register_touch(id, wl_fixed_to_int(sx), wl_fixed_to_int(sy));
}

void input_manager::propagate_touch_up(weston_touch* touch, uint32_t time,
        int32_t id)
{
    gr->last_time = time;
    gr->touch = touch;
    gr->unregister_touch(id);
}

void input_manager::propagate_touch_motion(weston_touch* touch, uint32_t time,
        int32_t id, wl_fixed_t sx, wl_fixed_t sy)
{
    gr->last_time = time;
    gr->touch = touch;
    gr->update_touch(id, wl_fixed_to_int(sx), wl_fixed_to_int(sy));

    if (gr->is_finger_sent_to_client(id)) {
        weston_touch_send_motion(touch, time, id, sx, sy);
    } else if(gr->is_finger_sent_to_grab(id)) {
        grab_send_touch_motion(touch, id, sx, sy);
    }
}


/* grab_send_touch_down/up/motion() are called from the gesture recognizer
 * in case they should be processed by plugin grabs */
void input_manager::grab_send_touch_down(weston_touch* touch, int32_t id,
        wl_fixed_t sx, wl_fixed_t sy)
{
    /* Important note to all such functions: we must copy grabs that receive
     * the callback in a separate vector, as grabs might be "removed" upon call
     * and thus our iterator might become invalid and crash the compositor */
    std::vector<wayfire_grab_interface> grabs;

    for (auto grab : active_grabs) {
        if (grab->callbacks.touch.down)
            grabs.push_back(grab);
    }

    for (auto grab : grabs)
        grab->callbacks.touch.down(touch, id, sx, sy);
}

void input_manager::grab_send_touch_up(weston_touch* touch, int32_t id)
{
    std::vector<wayfire_grab_interface> grabs;

    for (auto grab : active_grabs) {
        if (grab->callbacks.touch.up)
            grabs.push_back(grab);
    }

    for (auto grab : grabs)
        grab->callbacks.touch.up(touch, id);
}

void input_manager::grab_send_touch_motion(weston_touch* touch, int32_t id,
        wl_fixed_t sx, wl_fixed_t sy)
{
    std::vector<wayfire_grab_interface> grabs;

    for (auto grab : active_grabs) {
        if (grab->callbacks.touch.motion)
            grabs.push_back(grab);
    }

    for (auto grab : grabs)
        grab->callbacks.touch.motion(touch, id, sx, sy);
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

void pointer_grab_focus(weston_pointer_grab*) { }
void pointer_grab_axis(weston_pointer_grab *grab, uint32_t time, weston_pointer_axis_event *ev)
{
    core->input->propagate_pointer_grab_axis(grab->pointer, ev);
}
void pointer_grab_axis_source(weston_pointer_grab*, uint32_t) {}
void pointer_grab_frame(weston_pointer_grab*) {}
void pointer_grab_motion(weston_pointer_grab *grab, uint32_t time,
        weston_pointer_motion_event *ev)
{
    weston_pointer_move(grab->pointer, ev);
    core->input->propagate_pointer_grab_motion(grab->pointer, ev);
}
void pointer_grab_button(weston_pointer_grab *grab, uint32_t time,
        uint32_t button, uint32_t state)
{
    if (grab_start_finalized) {
        weston_compositor_run_button_binding(core->ec, grab->pointer,
                time, button, (wl_pointer_button_state) state);
    }
    core->input->propagate_pointer_grab_button(grab->pointer, button, state);
}
void pointer_grab_cancel(weston_pointer_grab *grab)
{
    core->input->end_grabs();
}

static const weston_pointer_grab_interface pointer_grab_interface = {
    pointer_grab_focus, pointer_grab_motion,      pointer_grab_button,
    pointer_grab_axis,  pointer_grab_axis_source, pointer_grab_frame,
    pointer_grab_cancel
};

/* keyboard grab callbacks */
void keyboard_grab_key(weston_keyboard_grab *grab, uint32_t time, uint32_t key,
                       uint32_t state)
{
    if (grab_start_finalized) {
        weston_compositor_run_key_binding(core->ec, grab->keyboard, time, key,
                (wl_keyboard_key_state)state);
    }
    core->input->propagate_keyboard_grab_key(grab->keyboard, key, state);
}
void keyboard_grab_mod(weston_keyboard_grab *grab, uint32_t time,
                       uint32_t depressed, uint32_t locked, uint32_t latched,
                       uint32_t group)
{
    core->input->propagate_keyboard_grab_mod(grab->keyboard, depressed, locked, latched, group);
}
void keyboard_grab_cancel(weston_keyboard_grab *)
{
    core->input->end_grabs();
}
static const weston_keyboard_grab_interface keyboard_grab_interface = {
    keyboard_grab_key, keyboard_grab_mod, keyboard_grab_cancel
};

bool input_manager::is_touch_enabled()
{
    return weston_seat_get_touch(core->get_current_seat()) != nullptr;
}

input_manager::input_manager()
{
    pgrab.interface = &pointer_grab_interface;
    kgrab.interface = &keyboard_grab_interface;

    if (is_touch_enabled())
    {

        auto touch = weston_seat_get_touch(core->get_current_seat());
        tgrab.interface = &touch_grab_interface;
        tgrab.touch = touch;

        touch->default_grab = tgrab;
        touch->grab = &tgrab;

        using namespace std::placeholders;
        gr = new wf_gesture_recognizer(touch,
                                       std::bind(std::mem_fn(&input_manager::handle_gesture),
                                                 this, _1));
    }
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

static void
idle_finalize_grab(void *data)
{
    grab_start_finalized = true;
}

void input_manager::grab_input(wayfire_grab_interface iface)
{
    if (!iface->grabbed)
        return;

    active_grabs.insert(iface);
    if (1 == active_grabs.size()) {
        auto ptr = weston_seat_get_pointer(core->get_current_seat());
        weston_pointer_start_grab(ptr, &pgrab);
        weston_keyboard_start_grab(weston_seat_get_keyboard(core->get_current_seat()),
                                   &kgrab);

        grab_start_finalized = false;

        wl_event_loop_add_idle(wl_display_get_event_loop(core->ec->wl_display),
                idle_finalize_grab, nullptr);

        auto background = core->get_active_output()->workspace->get_background_view();
        if (background)
            weston_pointer_set_focus(ptr, background->handle, -10000000, -1000000);

        if (is_touch_enabled())
            gr->start_grab();
    }
}

void input_manager::ungrab_input(wayfire_grab_interface iface)
{
    active_grabs.erase(iface);
    if (active_grabs.empty()) {
        weston_pointer_end_grab(weston_seat_get_pointer(core->get_current_seat()));
        weston_keyboard_end_grab(weston_seat_get_keyboard(core->get_current_seat()));

        if (is_touch_enabled())
            gr->end_grab();
    }
}

bool input_manager::input_grabbed()
{
    return !active_grabs.empty();
}

void input_manager::propagate_pointer_grab_axis(weston_pointer *ptr,
        weston_pointer_axis_event *ev)
{
    std::vector<wayfire_grab_interface> grab;
    for (auto x : active_grabs) {
        if (x->callbacks.pointer.axis)
            grab.push_back(x);
    }

    for (auto x : grab) x->callbacks.pointer.axis(ptr, ev);
}

void input_manager::propagate_pointer_grab_motion(
    weston_pointer *ptr, weston_pointer_motion_event *ev)
{
    std::vector<wayfire_grab_interface> grab;
    for (auto x : active_grabs) {
        if (x->callbacks.pointer.motion) grab.push_back(x);
    }

    for (auto x : grab) x->callbacks.pointer.motion(ptr, ev);
}

void input_manager::propagate_pointer_grab_button(weston_pointer *ptr,
        uint32_t button,
        uint32_t state)
{
    std::vector<wayfire_grab_interface> grab;
    for (auto x : active_grabs) {
        if (x->callbacks.pointer.button)
            grab.push_back(x);
    }

    for (auto x : grab)
        x->callbacks.pointer.button(ptr, button, state);
}

void input_manager::propagate_keyboard_grab_key(weston_keyboard *kbd,
        uint32_t key, uint32_t state)
{
    std::vector<wayfire_grab_interface> grab;
    for (auto x : active_grabs) {
        if (x->callbacks.keyboard.key) {
            grab.push_back(x);
        }
    }

    for (auto x : grab)
        x->callbacks.keyboard.key(kbd, key, state);
}

void input_manager::propagate_keyboard_grab_mod(weston_keyboard *kbd,
        uint32_t depressed, uint32_t locked, uint32_t latched, uint32_t group)
{
    std::vector<wayfire_grab_interface> grab;
    for (auto x : active_grabs) {
        if (x->callbacks.keyboard.mod)
            grab.push_back(x);
    }

    for (auto x : grab)
        x->callbacks.keyboard.mod(kbd, depressed, locked, latched, group);
}

void input_manager::end_grabs()
{
    std::vector<wayfire_grab_interface> v;

    for (auto x : active_grabs)
        v.push_back(x);

    for (auto x : v)
        ungrab_input(x);
}

struct key_callback_data {
    key_callback *call;
    wayfire_output *output;
};

static void keybinding_handler(weston_keyboard *kbd, uint32_t time, uint32_t key, void *data)
{
    auto ddata = (key_callback_data*) data;
    if (core->get_active_output() == ddata->output)
        (*ddata->call) (kbd, key);
}

struct button_callback_data {
    button_callback *call;
    wayfire_output *output;
};

static void buttonbinding_handler(weston_pointer *ptr, uint32_t time,
        uint32_t button, void *data)
{
    auto ddata = (button_callback_data*) data;
    if (core->get_active_output() == ddata->output)
        (*ddata->call) (ptr, button);
}

weston_binding* input_manager::add_key(uint32_t mod, uint32_t key,
        key_callback *call, wayfire_output *output)
{
    return weston_compositor_add_key_binding(core->ec, key,
            (weston_keyboard_modifier)mod, keybinding_handler, new key_callback_data {call, output});
}

weston_binding* input_manager::add_button(uint32_t mod,
        uint32_t button, button_callback *call, wayfire_output *output)
{
    return weston_compositor_add_button_binding(core->ec, button,
            (weston_keyboard_modifier)mod, buttonbinding_handler, new button_callback_data {call, output});
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
/* End input_manager */

void wayfire_core::configure(wayfire_config *config)
{
    this->config = config;
    auto section = config->get_section("core");

    vwidth  = section->get_int("vwidth", 3);
    vheight = section->get_int("vheight", 3);

    shadersrc = section->get_string("shadersrc", INSTALL_PREFIX "/share/wayfire/shaders");
    plugin_path = section->get_string("plugin_path_prefix", INSTALL_PREFIX "/lib/");
    plugins = section->get_string("plugins", "");

    run_panel = section->get_int("run_panel", 1);

    string model = section->get_string("xkb_model", "pc100");
    string variant = section->get_string("xkb_variant", "");
    string layout = section->get_string("xkb_layout", "us");
    string options = section->get_string("xkb_option", "");
    string rules = section->get_string("xkb_rule", "evdev");

    xkb_rule_names names;
    names.rules = strdup(rules.c_str());
    names.model = strdup(model.c_str());
    names.layout = strdup(layout.c_str());
    names.variant = strdup(variant.c_str());
    names.options = strdup(options.c_str());

    weston_compositor_set_xkb_rule_names(ec, &names);

    ec->kb_repeat_rate = section->get_int("kb_repeat_rate", 40);
    ec->kb_repeat_delay = section->get_int("kb_repeat_delay", 400);
}

void notify_output_created_idle_cb(void *data)
{
    core->for_each_output([] (wayfire_output *out) {
        wayfire_shell_send_output_created(core->wf_shell.resource,
                out->handle->id,
                out->handle->width, out->handle->height);
        if (out->handle->set_gamma) {
            wayfire_shell_send_gamma_size(core->wf_shell.resource,
                    out->handle->id, out->handle->gamma_size);
        }
    });
}

void unbind_desktop_shell(wl_resource *resource)
{
    core->wf_shell.client = NULL;
}

void bind_desktop_shell(wl_client *client, void *data, uint32_t version, uint32_t id)
{
    core->wf_shell.resource = wl_resource_create(client, &wayfire_shell_interface, 1, id);
    core->wf_shell.client = client;

    wl_resource_set_implementation(core->wf_shell.resource, &shell_interface_impl,
            NULL, unbind_desktop_shell);

    auto loop = wl_display_get_event_loop(core->ec->wl_display);
    wl_event_loop_add_idle(loop, notify_output_created_idle_cb, NULL);
}

void wayfire_core::init(weston_compositor *comp, wayfire_config *conf)
{
    ec = comp;
    configure(conf);

#if BUILD_WITH_IMAGEIO
    image_io::init();
#endif

    if (wl_global_create(ec->wl_display, &wayfire_shell_interface,
                1, NULL, bind_desktop_shell) == NULL) {
        errio << "Failed to create wayfire_shell interface" << std::endl;
    }
}

void refocus_idle_cb(void *data)
{
    core->refocus_active_output_active_view();
}

void wayfire_core::wake()
{
    if (times_wake == 0)
    {
        input = new input_manager();

        if (run_panel)
            run(INSTALL_PREFIX "/lib/wayfire/wayfire-shell-client");
    }

    for (auto out : pending_outputs)
        add_output(out);
    pending_outputs.clear();
    weston_compositor_wake(ec);

    ++times_wake;
    auto loop = wl_display_get_event_loop(ec->wl_display);
    wl_event_loop_add_idle(loop, refocus_idle_cb, 0);

    for_each_output([] (wayfire_output *output)
            { output->signal->emit_signal("wake", nullptr); });
}

void wayfire_core::sleep()
{
    for_each_output([] (wayfire_output *output)
            { output->signal->emit_signal("sleep", nullptr); });
    weston_compositor_sleep(ec);
}

/* FIXME: currently wayfire hijacks the built-in renderer, assuming that it is the gl-renderer
 * However, this isn't always true. Also, hijacking isn't the best option
 * Maybe we should draw to a surface and display it? */
void repaint_output_callback(weston_output *o, pixman_region32_t *damage)
{
    auto output = core->get_output(o);
    if (output)
    {
        output->render->pre_paint();
        output->render->paint(damage);
    }
}

void wayfire_core::hijack_renderer()
{
    weston_renderer_repaint = core->ec->renderer->repaint_output;
    core->ec->renderer->repaint_output = repaint_output_callback;
}

void wayfire_core::weston_repaint(weston_output *output, pixman_region32_t *damage)
{
    weston_renderer_repaint(output, damage);
}

weston_seat* wayfire_core::get_current_seat()
{
    weston_seat *seat;
    wl_list_for_each(seat, &ec->seat_list, link) {
        return seat;
    }
    return nullptr;
}

void wayfire_core::add_output(weston_output *output)
{
    debug << "Adding output " << output->id << std::endl;
    if (outputs.find(output->id) != outputs.end())
        return;

    if (!input) {
        pending_outputs.push_back(output);
        return;
    }

    outputs[output->id] = new wayfire_output(output, config);
    focus_output(outputs[output->id]);

    if (wf_shell.client)
        wayfire_shell_send_output_created(wf_shell.resource, output->id,
                output->width, output->height);

    weston_output_schedule_repaint(output);
}

void wayfire_core::refocus_active_output_active_view()
{
    if (!active_output)
        return;

    auto view = active_output->get_top_view();
    if (view) {
        active_output->focus_view(nullptr, get_current_seat());
        active_output->focus_view(view, get_current_seat());
    }
}

void wayfire_core::focus_output(wayfire_output *wo)
{
    assert(wo);
    if (active_output == wo)
        return;

    wo->ensure_pointer();

    if (active_output)
        active_output->focus_view(nullptr, get_current_seat());

    active_output = wo;
    refocus_active_output_active_view();

    if (wo)
        debug << "focus output: " << wo->handle->id << std::endl;

    if (active_output)
        weston_output_schedule_repaint(active_output->handle);
}

wayfire_output* wayfire_core::get_output(weston_output *handle)
{
    auto it = outputs.find(handle->id);
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
    auto id = output->handle->id;
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

void wayfire_core::add_view(weston_desktop_surface *ds)
{
    auto view = std::make_shared<wayfire_view_t> (ds);
    views[view->handle] = view;

    if (active_output)
        active_output->attach_view(view);

    focus_view(view, get_current_seat());
}

wayfire_view wayfire_core::find_view(weston_view *handle)
{
    auto it = views.find(handle);
    if (it == views.end()) {
        return nullptr;
    } else {
        return it->second;
    }
}

wayfire_view wayfire_core::find_view(weston_desktop_surface *desktop_surface)
{
    for (auto v : views)
        if (v.second->desktop_surface == desktop_surface)
            return v.second;

    return nullptr;
}

wayfire_view wayfire_core::find_view(weston_surface *surface)
{
    for (auto v : views)
        if (v.second->surface == surface)
            return v.second;

    return nullptr;
}

void wayfire_core::focus_view(wayfire_view v, weston_seat *seat)
{
    if (!v)
        return;

    if (v->output != active_output)
        focus_output(v->output);

    active_output->focus_view(v, seat);
}

void wayfire_core::close_view(wayfire_view v)
{
    if (!v)
       return;

    weston_desktop_surface_close(v->desktop_surface);
}

void wayfire_core::erase_view(wayfire_view v)
{
    if (!v) return;

    views.erase(v->handle);
    v->output->detach_view(v);
    if (v->handle)
        weston_view_destroy(v->handle);
}

void wayfire_core::run(const char *command)
{
    std::string cmd = command;
    cmd = "WAYLAND_DISPLAY=" + wayland_display + " " + cmd;
    pid_t pid = fork();

    /* The following is a "hack" for disowning the child processes,
     * otherwise they will simply stay as zombie processes */
    if (!pid) {
        if (!fork()) {
            exit(execl("/bin/sh", "/bin/sh", "-c", cmd.c_str(), NULL));
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
    if (v->output)
    {
        v->output->detach_view(v);
    }

    if (new_output) {
        new_output->attach_view(v);
    } else {
        close_view(v);
    }
}

wayfire_core *core;
