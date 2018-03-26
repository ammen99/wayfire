#ifndef OUTPUT_HPP
#define OUTPUT_HPP

#include "plugin.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <view.hpp>

extern "C"
{
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_seat.h>
}

struct plugin_manager;

class workspace_manager;
class render_manager;

class wayfire_view_t;
using wayfire_view = std::shared_ptr<wayfire_view_t>;

class wayfire_output
{
    friend class wayfire_core;

    private:
       std::unordered_map<std::string, std::vector<signal_callback_t*>> signals;
       std::unordered_multiset<wayfire_grab_interface> active_plugins;

       plugin_manager *plugin;
       wayfire_view active_view;

       /* return an active wayfire_grab_interface on this output
        * which has grabbed the input. If none, then return nullptr */
       wayfire_grab_interface get_input_grab_interface();

       wl_listener destroy_listener;

    public:
       int id;
       wlr_output* handle;
       std::tuple<int, int> get_screen_size();

       render_manager *render;
       workspace_manager *workspace;

       wayfire_output(wlr_output*, wayfire_config *config);
       ~wayfire_output();
       wf_geometry get_full_geometry();

       void set_transform(wl_output_transform new_transform);
       wl_output_transform get_transform();
       /* makes sure that the pointer is inside the output's geometry */
       void ensure_pointer();

       /* @param break_fs - lower fullscreen windows if any */
       bool activate_plugin  (wayfire_grab_interface owner, bool lower_fs = true);
       bool deactivate_plugin(wayfire_grab_interface owner);
       bool is_plugin_active (owner_t owner_name);

       void connect_signal(std::string name, signal_callback_t* callback);
       void disconnect_signal(std::string name, signal_callback_t* callback);
       void emit_signal(std::string name, signal_data *data);

       void activate();
       void deactivate();

       wayfire_view get_top_view();
       wayfire_view get_view_at_point(int x, int y);

       void attach_view(wayfire_view v);
       void detach_view(wayfire_view v);

       void focus_view(wayfire_view v, wlr_seat *seat = nullptr);
       void set_active_view(wayfire_view v);
       void bring_to_front(wayfire_view v);

       int add_key(uint32_t mod, uint32_t key, key_callback *);
       int add_button(uint32_t mod, uint32_t button, button_callback *);

       int  add_touch(uint32_t mod, touch_callback*);
       void rem_touch(int32_t id);

       /* we take only gesture type and finger count into account,
        * we send for all possible directions */
       int add_gesture(const wayfire_touch_gesture& gesture, touch_gesture_callback* callback);
       void rem_gesture(int id);
};
extern const struct wayfire_shell_interface shell_interface_impl;
#endif /* end of include guard: OUTPUT_HPP */
