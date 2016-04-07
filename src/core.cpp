#include "core.hpp"
#include "output.hpp"

class CorePlugin : public Plugin {
    public:
        void init() {
            options.insert(newIntOption("vwidth", 3));
            options.insert(newIntOption("vheight", 3));
            options.insert(newStringOption("background", ""));
            options.insert(newStringOption("shadersrc", "/usr/share/"));
            options.insert(newStringOption("pluginpath", "/usr/lib/"));
            options.insert(newStringOption("plugins", ""));
            options.insert(newStringOption("key_repeat_rate", "50"));
            options.insert(newStringOption("key_repeat_delay", "350"));
        }
        void initOwnership() {
            owner->name = "core";
            owner->compatAll = true;
        }
        void updateConfiguration() {
            core->vwidth  = options["vwidth"]->data.ival;
            core->vheight = options["vheight"]->data.ival;

            core->background  = *options["background"]->data.sval;
            core->shadersrc   = *options["shadersrc"]->data.sval;
            core->plugin_path = *options["pluginpath"]->data.sval;
            core->plugins     = *options["plugins"]->data.sval;

            setenv("WLC_REPEAT_RATE", options["key_repeat_rate"]->data.sval->c_str(), 1);
            setenv("WLC_REPEAT_DELAY", options["key_repeat_delay"]->data.sval->c_str(), 1);
        }
};

Core *core;

void Core::init() {
    PluginPtr plug = std::static_pointer_cast<Plugin>(std::make_shared<CorePlugin>());
    plug->owner = std::make_shared<_Ownership>();
    plug->initOwnership();
    plug->init();

    config = new Config("/home/ilex/.config/firerc");
    config->setOptionsForPlugin(plug);

    plug->updateConfiguration();
    plug.reset();
}

void Core::add_output(wlc_handle o) {
    if (outputs.find(o) != outputs.end())
        return;

    outputs.insert(std::make_pair(o, new Output(o, config)));
    wlc_output_set_mask(o, 1);
    focus_output(outputs[o]);
}

void Core::focus_output(Output *o) {
    if (!o)
        return;

    wlc_output_focus(o->get_handle());
    active_output = o;
}

Output* Core::get_output(wlc_handle handle) {
    auto it = outputs.find(handle);
    if (it != outputs.end()) {
        return it->second;
    } else {
        return nullptr;
    }
}

Output* Core::get_active_output() {
    return active_output;
}

Output* Core::get_next_output() {
    auto id = active_output->id;
    auto it = outputs.find(id);
    ++it;

    if (it == outputs.end()) {
        return outputs.begin()->second;
    } else {
        return it->second;
    }
}

void Core::for_each_output(OutputCallbackProc call) {
    for (auto o : outputs)
        call(o.second);
}

void Core::add_view(wlc_handle view) {
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

View Core::find_view(wlc_handle handle) {
    auto it = views.find(handle);
    if (it == views.end()) {
        return nullptr;
    } else {
        return it->second;
    }
}

void Core::focus_view(View v) {
    if (!v)
        return;

    if (v->output != active_output)
        focus_output(v->output);

    active_output->focus_view(v);
}

void Core::close_view(View v) {
    if (!v)
       return;

    wlc_view_close(v->get_id());
    focus_view(active_output->get_active_view());
}

void Core::rem_view(wlc_handle v) {
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

void Core::erase_view(wlc_handle v) {
    views.erase(v);
}

namespace {
    int last_id = 0;
}

uint32_t Core::get_nextid() {
    return ++last_id;
}

void Core::run(const char *command) {
    auto pid = fork();

    if (!pid)
        std::exit(execl("/bin/sh", "/bin/sh", "-c", command, NULL));
}

void Core::move_view_to_output(View v, Output *old, Output *new_output) {
    if (old && v->output && old->id == v->output->id)
        old->detach_view(v);

    if (new_output) {
        new_output->attach_view(v);
    } else {
        close_view(v);
    }
}


