#ifndef WM_H
#define WM_H
#include "plugin.hpp"
#include "core.hpp"

// The following plugins
// are very, very simple(they just register a keybinding)
class Exit : public wayfire_plugin {
    public:
        void init();
};

class Close : public wayfire_plugin {
    public:
        void init();
};

class Refresh : public wayfire_plugin { // keybinding to restart window manager
    public:
        void init();
};

class Focus : public wayfire_plugin {
    public:
        void init();
};
#endif
