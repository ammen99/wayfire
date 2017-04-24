#include "core.hpp"
#include "output.hpp"
#include "img.hpp"
#include <unistd.h>
#include "../proto/wayfire-shell-server.h"
#include "signal_definitions.hpp"
/* Manages input grabs */
/* TODO: make compatible with weston */

/* Start input_manager */

/* pointer grab callbacks */
void pointer_grab_focus(weston_pointer_grab*) { }
void pointer_grab_axis(weston_pointer_grab *grab, uint32_t time, weston_pointer_axis_event *ev)
{
    core->input->propagate_pointer_grab_axis(grab->pointer, ev);
}
void pointer_grab_axis_source(weston_pointer_grab*, uint32_t) {}
void pointer_grab_frame(weston_pointer_grab*) {}
void pointer_grab_motion(weston_pointer_grab *grab, uint32_t time, weston_pointer_motion_event *ev)
{
    weston_pointer_move(grab->pointer, ev);
    core->input->propagate_pointer_grab_motion(grab->pointer, ev);
}
void pointer_grab_button(weston_pointer_grab *grab, uint32_t, uint32_t b, uint32_t s)
{
    core->input->propagate_pointer_grab_button(grab->pointer, b, s);
}
void pointer_grab_cancel(weston_pointer_grab *grab)
{
    core->input->end_grabs();
}

namespace
{
const weston_pointer_grab_interface pointer_grab_interface = {
    pointer_grab_focus, pointer_grab_motion,      pointer_grab_button,
    pointer_grab_axis,  pointer_grab_axis_source, pointer_grab_frame,
    pointer_grab_cancel
};
}

/* keyboard grab callbacks */
void keyboard_grab_key(weston_keyboard_grab *grab, uint32_t time, uint32_t key,
                       uint32_t state)
{
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
namespace
{
const weston_keyboard_grab_interface keyboard_grab_interface = {
    keyboard_grab_key, keyboard_grab_mod, keyboard_grab_cancel
};
}

input_manager::input_manager()
{
    pgrab.interface = &pointer_grab_interface;
    kgrab.interface = &keyboard_grab_interface;
}

void input_manager::grab_input(wayfire_grab_interface iface)
{
    if (!iface->grabbed)
        return;

    active_grabs.insert(iface);
    if (1 == active_grabs.size()) {
        weston_pointer_start_grab(weston_seat_get_pointer(core->get_current_seat()),
                                  &pgrab);
        weston_keyboard_start_grab(weston_seat_get_keyboard(core->get_current_seat()),
                                   &kgrab);
    }
}

void input_manager::ungrab_input(wayfire_grab_interface iface)
{
    active_grabs.erase(iface);
    if (active_grabs.empty()) {
        weston_pointer_end_grab(weston_seat_get_pointer(core->get_current_seat()));
        weston_keyboard_end_grab(weston_seat_get_keyboard(core->get_current_seat()));
    }
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
/* End input_manager */

void wayfire_core::configure(wayfire_config *config)
{
    this->config = config;
    auto section = config->get_section("core");

    vwidth  = section->get_int("vwidth", 3);
    vheight = section->get_int("vheight", 3);
    background = section->get_string("background", "");

    shadersrc = section->get_string("shadersrc", "/usr/share/wayfire/shaders");
    plugin_path = section->get_string("plugin_path_prefix", "/usr/lib/");
    plugins = section->get_string("plugins", "");

    run_panel = section->get_int("run_panel", 1);

    /*
       options.insert(newStringOption("key_repeat_rate", "50"));
       options.insert(newStringOption("key_repeat_delay", "350"));

       options.insert(newStringOption("kbd_model", "pc100"));
       options.insert(newStringOption("kbd_layouts", "us"));
       options.insert(newStringOption("kbd_variants", ""));
       options.insert(newStringOption("kbd_options", "grp:win_space_toggle"));
       */
}

void notify_output_created_idle_cb(void *data)
{
    core->for_each_output([] (wayfire_output *out) {
            wayfire_shell_send_output_created(core->wf_shell.resource,
                    out->handle->id,
                    out->handle->width, out->handle->height);
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
    image_io::init();

    if (wl_global_create(ec->wl_display, &wayfire_shell_interface,
                1, NULL, bind_desktop_shell) == NULL) {
        errio << "Failed to create wayfire_shell interface" << std::endl;
    }

    input = new input_manager();
}

void wayfire_core::wake()
{
    debug << "compositor wake " << times_wake << " " << run_panel << std::endl;
    if (times_wake == 0 && run_panel)
        run(("/usr/lib/wayfire/wayfire-shell-client -b " + background).c_str());

    ++times_wake;
    refocus_active_output_active_view();
}

void wayfire_core::sleep()
{
}

/* TODO: currently wayfire hijacks the built-in renderer, assuming that it is the gl-renderer
 * However, this isn't always true. Also, hijacking isn't the best option
 * We should render to a surface which is made standalone */
void repaint_output_callback(weston_output *o, pixman_region32_t *damage)
{
    auto output = core->get_output(o);
    if (output) {
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

    auto ptr = weston_seat_get_pointer(get_current_seat());
    int px = wl_fixed_to_int(ptr->x), py = wl_fixed_to_int(ptr->y);

    auto g = wo->get_full_geometry();
    if (!point_inside({px, py}, g)) {
        wl_fixed_t cx = wl_fixed_from_int(g.origin.x + g.size.w / 2);
        wl_fixed_t cy = wl_fixed_from_int(g.origin.y + g.size.h / 2);

        weston_pointer_motion_event ev;
        ev.mask |= WESTON_POINTER_MOTION_ABS;
        ev.x = wl_fixed_to_double(cx);
        ev.y = wl_fixed_to_double(cy);

        weston_pointer_move(ptr, &ev);
    }


    debug << "focus_output old: " << (active_output ? active_output->handle->id : -1)
        << " new output: " << wo->handle->id << std::endl;
    if (active_output)
        active_output->focus_view(nullptr, get_current_seat());

    active_output = wo;
    refocus_active_output_active_view();
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
}

void wayfire_core::run(const char *command)
{
    debug << "run " << command << std::endl;

    std::string cmd = command;
    cmd = "WAYLAND_DISPLAY=" + wayland_display + " " + cmd;
    debug << "full cmd: " << cmd << std::endl;
    auto pid = fork();

    if (!pid) {
        std::exit(execl("/bin/sh", "/bin/sh", "-c", cmd.c_str(), NULL));
    }
}

void wayfire_core::move_view_to_output(wayfire_view v, wayfire_output *old, wayfire_output *new_output)
{
    if (old && v->output && old == v->output)
        old->detach_view(v);

    if (new_output) {
        new_output->attach_view(v);
    } else {
        close_view(v);
    }
}

wayfire_core *core;
