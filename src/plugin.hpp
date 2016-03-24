#ifndef PLUGIN_H
#define PLUGIN_H

#include "view.hpp"

using std::string;
/* a useful macro for making animation/hook duration
 * independent of refresh rate
 * x is in milliseconds(for example see Expo) */
#define getSteps(x) ((x)/(1000/core->getRefreshRate()))

/*
 * Documentation for writing a plugin
 *
 * Plugins are just objects which are created during init of
 * Core and destroyed when core is destroyed
 *
 * Plugins aren't run at any time(i.e between redraws)
 * They must have at least the method init()
 * which is the method when they register on the Core()
 * their bindings and hooks
 *
 * They should not atempt to access the global core variable
 * in their constructors since during that time that global
 * variable is still uninitialized
 *
 * A typical example of a plugin is when
 * in the init() function it registers a hook(disabled)
 * and then a key/button binding to activate it
 *
 * For example see expo.cpp, move.cpp, resize.cpp
 */

class Output;

using Owner = string;

/* owners are used to acquire screen grab and to activate */
struct _Ownership {
    Owner name;
    std::unordered_set<Owner> compat;
    bool compatAll = true;
    Output *output;

    void grab();
    void ungrab();
    bool grabbed;
};

using Ownership = std::shared_ptr<_Ownership>;


enum DataType { DataTypeBool, DataTypeInt,
                DataTypeFloat, DataTypeString,
                DataTypeColor, DataTypeKey,
                DataTypeButton};

struct Color {
    float r, g, b;
};
struct Key {
    uint mod;
    int key;
};
struct Button {
    uint mod;
    int button;
};

union SubData {
    bool  bval;
    int   ival;
    float fval;

    string *sval;
    Color  *color;
    Key    *key;
    Button *but;

    SubData();
    ~SubData();
};

struct Data {
    DataType type;
    SubData data, def;

    /* plugins should not touch this,
     * except for the case when they want the
     * option to not be read from config file */
    bool alreadySet = false;
    Data();
};

using DataPair = std::pair<string, Data*>;
/* helper functions to easily add options */
DataPair newIntOption   (string name, int    defaultVal);
DataPair newFloatOption (string name, float  defaultVal);
DataPair newBoolOption  (string name, bool   defaultVal);
DataPair newStringOption(string name, string defaultVal);
DataPair newColorOption (string name, Color  defaultVal);
DataPair newKeyOption   (string name, Key    defaultVal);
DataPair newButtonOption(string name, Button defaultVal);

void readColor  (string value, Color &colorval);
void readKey    (string value, Key   &keyval  );
void readButton (string value, Key   &butval  );

class Plugin {
    public:
        /* Output this plugin is running on */
        Output *output;

        std::unordered_map<string, Data*> options;

        /* initOwnership() should set all values in own */
        virtual void initOwnership();

        /* each plugin should allocate all options and set their
         * type and def value in init().
         * After that they are automatically read
         * and if not available, the data becomes def */
        virtual void init() = 0;

        /* used to obtain already read options */
        virtual void updateConfiguration();

        /* the fini() method should remove all hooks/buttons/keys
         * and of course prepare the plugin for deletion, i.e
         * fini() must act like destuctor */
        virtual void fini();
        Ownership owner;

        /* plugin must not touch these! */
        bool dynamic = false;
        void *handle;
};

using PluginPtr = std::shared_ptr<Plugin>;
/* each dynamic plugin should have the symbol loadFunction() which returns
 * an instance of the plugin */
typedef Plugin *(*LoadFunction)();

struct EventContext {
    struct {
        struct {
            int x_root, y_root;
        } xbutton;

        struct {
            uint32_t key;
            uint32_t mod;
        } xkey;
    } xev;

    EventContext(int x, int y, int key, int mod) {
        xev.xbutton.x_root = x;
        xev.xbutton.y_root = y;

        xev.xkey.key = key;
        xev.xkey.mod = mod;
    }
};

enum BindingType {
    BindingTypePress,
    BindingTypeRelease
};

struct Binding{
    bool active = false;
    BindingType type;
    uint32_t mod;
    uint id;
    std::function<void(EventContext)> action;
};

struct KeyBinding : Binding {
    uint32_t key;

    void enable();
    void disable();
};

struct ButtonBinding : Binding {
    uint32_t button;

    void enable();
    void disable();
};

// hooks are used to handle pointer motion
struct Hook {
    protected:
        bool active = false;
    public:
        uint id;
        std::function<void(void)> action;

        virtual void enable();
        virtual void disable();
        bool getState();
        Hook();
};

/* render hooks are used to replace the renderAllWindows()
 * when necessary, i.e to render completely custom
 * image */
using RenderHook = std::function<void()>;

/* Effects are used to draw over the screen, e.g when animating window actions */
/* Effects render directly to Core's framebuffer
 * They are either as overlays, which means they are always drawn
 * or they are on per-window basis */

enum EffectType { EFFECT_OVERLAY, EFFECT_WINDOW };
struct EffectHook : public Hook {
    EffectType type;

    /* used only if type == EFFECT_WINDOW */
    View win;
};

using SignalListenerData = std::vector<void*>;

struct SignalListener {
    std::function<void(SignalListenerData)> action;
    uint id;
};

#define GetTuple(x,y,t) auto x = std::get<0>(t); \
                        auto y = std::get<1>(t)

using WindowCallbackProc = std::function<void(View)>;



#endif
