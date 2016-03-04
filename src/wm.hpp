#ifndef WM_H
#define WM_H
#include "plugin.hpp"
#include "core.hpp"

// The following plugins
// are very, very simple(they just register a keybinding)
class Exit : public Plugin {
    public:
        void init();
};

class Close : public Plugin {
    public:
        void init();
};

class Refresh : public Plugin { // keybinding to restart window manager
    KeyBinding ref;
    public:
        void init();
};

class Focus : public Plugin {
    private:
        ButtonBinding focus;
    public:
        void init();
};
#endif
