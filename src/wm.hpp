#ifndef WM_H
#define WM_H
#include "plugin.hpp"
#include "core.hpp"

class wayfire_close : public wayfire_plugin_t {
    key_callback callback;
    public:
        void init(wayfire_config*);
};

class wayfire_focus : public wayfire_plugin_t {
    button_callback callback;
    touch_callback touch;
    public:
        void init(wayfire_config*);
};

class wayfire_exit : public wayfire_plugin_t {
    key_callback key;
    public:
        void init(wayfire_config*);
};

class wayfire_fullscreen : public wayfire_plugin_t {
    signal_callback_t act_request;
    public:
    void init(wayfire_config *config);
};

#endif
