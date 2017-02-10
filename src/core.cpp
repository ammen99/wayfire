#include "core.hpp"
#include "output.hpp"
#include "img.hpp"

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
void wayfire_core::init(weston_config *conf) {
    configure(conf);
    image_io::init();
}

void wayfire_core::add_output(weston_output *output) {
    weston_view *v;
    if (outputs.find(output->id) != outputs.end())
        return;

    outputs[output->id] = new wayfire_output(output, config);
    wlc_output_set_mask(o, 1);
    focus_output(outputs[o]);
}

void wayfire_core::focus_output(Output *o) {
    if (!o)
        return;

    wlc_output_focus(o->get_handle());
    active_output = o;
}

Output* wayfire_core::get_output(wlc_handle handle) {
    auto it = outputs.find(handle);
    if (it != outputs.end()) {
        return it->second;
    } else {
        return nullptr;
    }
}

Output* wayfire_core::get_active_output() {
    return active_output;
}

Output* wayfire_core::get_next_output() {
    auto id = active_output->id;
    auto it = outputs.find(id);
    ++it;

    if (it == outputs.end()) {
        return outputs.begin()->second;
    } else {
        return it->second;
    }
}

void wayfire_core::for_each_output(OutputCallbackProc call) {
    for (auto o : outputs)
        call(o.second);
}

void wayfire_core::add_view(wlc_handle view) {
    View v = std::make_shared<FireView>(view);

    views[view] = v;
    if (active_output)
        active_output->attach_view(v);

    wlc_view_bring_to_front(view);
    uint32_t type = wlc_view_get_type(view);

    switch (type) {
        case 0:
        case WLC_BIT_MODAL:
        case WLC_BIT_OVERRIDE_REDIRECT:
            wlc_view_focus(view);
            wlc_view_set_state(view, WLC_BIT_ACTIVATED, true);
            break;
        /* popups and others */
        default:
            break;
    }
}

View wayfire_core::find_view(wlc_handle handle) {
    auto it = views.find(handle);
    if (it == views.end()) {
        return nullptr;
    } else {
        return it->second;
    }
}

void wayfire_core::focus_view(View v) {
    if (!v)
        return;

    if (v->output != active_output)
        focus_output(v->output);

    active_output->focus_view(v);
}

void wayfire_core::close_view(View v) {
    if (!v)
       return;

    wlc_view_close(v->get_id());
    focus_view(active_output->get_active_view());
}

void wayfire_core::rem_view(wlc_handle v) {
    auto it = views.find(v);
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

void wayfire_core::erase_view(wlc_handle v) {
    views.erase(v);
}

namespace {
    int last_id = 0;
}

uint32_t wayfire_core::get_nextid() {
    return ++last_id;
}

void wayfire_core::run(const char *command) {
    auto pid = fork();

    if (!pid)
        std::exit(execl("/bin/sh", "/bin/sh", "-c", command, NULL));
}

void wayfire_core::move_view_to_output(View v, Output *old, Output *new_output) {
    if (old && v->output && old->id == v->output->id)
        old->detach_view(v);

    if (new_output) {
        new_output->attach_view(v);
    } else {
        close_view(v);
    }
}

wayfire_core *core;
