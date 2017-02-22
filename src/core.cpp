#include "core.hpp"
#include "output.hpp"
#include "img.hpp"
#include <unistd.h>

void wayfire_core::configure(weston_config *config) {
    this->config = config;
    weston_config_section *sect =
        weston_config_get_section(config, "core", NULL, NULL);

    weston_config_section_get_int(sect, "vwidth", &core->vwidth, 3);
    weston_config_section_get_int(sect, "vheight", &core->vheight, 3);

    weston_config_section_get_cppstring(sect, "background", core->background, "");
    weston_config_section_get_cppstring(sect, "shadersrc", core->shadersrc, "/usr/share/wayfire/shaders");
    weston_config_section_get_cppstring(sect, "pluginpathprefix", core->plugin_path, "/usr/lib/");
    weston_config_section_get_cppstring(sect, "plugins", core->plugins, "");

    /*
       options.insert(newStringOption("key_repeat_rate", "50"));
       options.insert(newStringOption("key_repeat_delay", "350"));

       options.insert(newStringOption("kbd_model", "pc100"));
       options.insert(newStringOption("kbd_layouts", "us"));
       options.insert(newStringOption("kbd_variants", ""));
       options.insert(newStringOption("kbd_options", "grp:win_space_toggle"));
       */
}
void wayfire_core::init(weston_compositor *comp, weston_config *conf) {
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

void wayfire_core::for_each_output(output_callback_proc call) {
    for (auto o : outputs)
        call(o.second);
}


void wayfire_core::add_view(weston_desktop_surface *ds) {

    /* TODO: move view initialization to wayfire_view_t constructor */
    auto view = weston_desktop_surface_create_view(ds);
    view->plane = view->plane; /* workaround for compiler warning */

    weston_desktop_surface_set_user_data(ds, NULL);
    weston_desktop_surface_set_activated(ds, true);

    wayfire_view v = std::make_shared<wayfire_view_t> (view);

    views[view] = v;
    v->desktop_surface = ds;
    v->surface = weston_desktop_surface_get_surface(ds);
    v->handle = view;

    if (active_output)
        active_output->attach_view(v);

    focus_view(v, get_current_seat());
}

wayfire_view wayfire_core::find_view(weston_view *handle) {
    auto it = views.find(handle);
    if (it == views.end()) {
        return nullptr;
    } else {
        return it->second;
    }
}

wayfire_view wayfire_core::find_view(weston_desktop_surface *desktop_surface) {
    for (auto v : views)
        if (v.second->desktop_surface == desktop_surface)
            return v.second;

    return nullptr;
}

void wayfire_core::focus_view(wayfire_view v, weston_seat *seat) {
    if (!v)
        return;

    if (v->output != active_output)
        focus_output(v->output);

    /* TODO: get current seat -> keyboard -> set_keyboard_focus */
    active_output->focus_view(v, seat);
}

void wayfire_core::close_view(wayfire_view v) {
    if (!v)
       return;

    weston_desktop_surface_close(v->desktop_surface);
    /* XXX: maybe wait a little bit for the view to close, possibly ignore the closed view */
    focus_view(active_output->get_active_view(), get_current_seat());
}

void wayfire_core::erase_view(wayfire_view v) {
    if (!v) return;
    auto it = views.find(v->handle);
    if (it != views.end()) {
        auto view = it->second;

        view->destroyed = true;
        view->output->detach_view(view);
        if (view->keep_count == 0) {
            it->second.reset();
            views.erase(it);
        }
    }
}

namespace {
    int last_id = 0;
}

void wayfire_core::run(const char *command) {
    auto pid = fork();

    if (!pid)
        std::exit(execl("/bin/sh", "/bin/sh", "-c", command, NULL));
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
