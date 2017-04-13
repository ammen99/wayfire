#include "core.hpp"
#include "output.hpp"
#include "img.hpp"
#include <unistd.h>

void wayfire_core::configure(wayfire_config *config) {
    this->config = config;
    auto section = config->get_section("core");

    vwidth  = section->get_int("vwidth", 3);
    vheight = section->get_int("vheight", 3);
    background = section->get_string("background", "");

    shadersrc = section->get_string("shadersrc", "/usr/share/wayfire/shaders");
    plugin_path = section->get_string("plugin_path_prefix", "/usr/lib/");
    plugins = section->get_string("plugins", "");
    debug << "plugins are " << plugins << std::endl;

    /*
       options.insert(newStringOption("key_repeat_rate", "50"));
       options.insert(newStringOption("key_repeat_delay", "350"));

       options.insert(newStringOption("kbd_model", "pc100"));
       options.insert(newStringOption("kbd_layouts", "us"));
       options.insert(newStringOption("kbd_variants", ""));
       options.insert(newStringOption("kbd_options", "grp:win_space_toggle"));
       */
}
void wayfire_core::init(weston_compositor *comp, wayfire_config *conf) {
    ec = comp;
    configure(conf);
    image_io::init();
}

weston_seat* wayfire_core::get_current_seat() {
    weston_seat *seat;
    wl_list_for_each(seat, &ec->seat_list, link) {
        return seat;
    }
    return nullptr;
}

void wayfire_core::add_output(weston_output *output) {
    debug << "add output" << std::endl;
    if (outputs.find(output->id) != outputs.end())
        return;

    outputs[output->id] = new wayfire_output(output, config);
    focus_output(outputs[output->id]);
}

void wayfire_core::focus_output(wayfire_output *o) {
    if (!o)
        return;

    active_output = o;
}

wayfire_output* wayfire_core::get_output(weston_output *handle) {
    auto it = outputs.find(handle->id);
    if (it != outputs.end()) {
        return it->second;
    } else {
        return nullptr;
    }
}

wayfire_output* wayfire_core::get_active_output() {
    return active_output;
}

wayfire_output* wayfire_core::get_next_output() {
    auto id = active_output->handle->id;
    auto it = outputs.find(id);
    ++it;

    if (it == outputs.end()) {
        return outputs.begin()->second;
    } else {
        return it->second;
    }
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

void wayfire_core::focus_view(wayfire_view v, weston_seat *seat)
{
    if (!v)
        return;

    if (v->output != active_output)
        focus_output(v->output);

    active_output->focus_view(v, seat);
}

void wayfire_core::close_view(wayfire_view v) {
    if (!v)
       return;

    weston_desktop_surface_close(v->desktop_surface);
}

void wayfire_core::erase_view(wayfire_view v) {
    if (!v) return;
    views.erase(v->handle);
}

void wayfire_core::run(const char *command) {
    debug << "run " << command << std::endl;

    std::string cmd = command;
    cmd = "WAYLAND_DISPLAY=" + wayland_display + " " + cmd;
    debug << "full cmd: " << cmd << std::endl;
    auto pid = fork();

    if (!pid) {
        std::exit(execl("/bin/sh", "/bin/sh", "-c", cmd.c_str(), NULL));
    }
}

void wayfire_core::move_view_to_output(wayfire_view v, wayfire_output *old, wayfire_output *new_output) {
    if (old && v->output && old == v->output)
        old->detach_view(v);

    if (new_output) {
        new_output->attach_view(v);
    } else {
        close_view(v);
    }
}

wayfire_core *core;
