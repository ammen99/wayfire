#ifndef FIRE_H
#define FIRE_H

#include "plugin.hpp"
#include "config.hpp"

#include <map>

using OutputCallbackProc = std::function<void(Output*)>;

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

    /* Only removes the view from the "database".
     * Use only when view is already destroyed and detached from output */
    void erase_view(wlc_handle view);

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

    void for_each_output(OutputCallbackProc);

    void run(const char *command);

    uint32_t get_nextid();

    int vwidth, vheight;
    std::string background, shadersrc, plugin_path, plugins;
};

extern Core *core;
#endif
