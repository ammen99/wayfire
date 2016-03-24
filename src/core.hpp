#ifndef FIRE_H
#define FIRE_H

#include "plugin.hpp"
#include "config.hpp"

#include <map>

class Core {
    Config *config;
    uint32_t nextID = 0;

    Output *active_output;
    std::map<wlc_handle, Output*> outputs;
    std::unordered_map<wlc_handle, View> views;

    public:
    void init();

    View find_view(wlc_handle handle);
    void add_view(wlc_handle view);
    void rem_view(wlc_handle view);

    /* brings the view to the top
     * and also focuses its output */
    void focus_view(View win);
    void close_view(View win);
    void move_view_to_output(View v, Output *old, Output *new_output);

    void add_output(wlc_handle o);
    Output* get_output(wlc_handle o);
    void focus_output(Output* o);
    void rem_output(Output* o);
    Output *get_active_output();
    Output *get_next_output();

    void run(const char *command);

    uint32_t get_nextid();

    int vwidth, vheight;
    std::string background, shadersrc, plugin_path, plugins;
};

extern Core *core;

class CorePlugin : public Plugin {
    public:
        void init() {
            options.insert(newIntOption("vwidth", 3));
            options.insert(newIntOption("vheight", 3));
            options.insert(newStringOption("background", ""));
            options.insert(newStringOption("shadersrc", "/usr/local/share/"));
            options.insert(newStringOption("pluginpath", "/usr/local/lib/"));
            options.insert(newStringOption("plugins", ""));
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
        }
};


#endif
