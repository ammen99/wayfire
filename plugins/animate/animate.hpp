#ifndef ANIMATE_H_
#define ANIMATE_H_

#include <core.hpp>

class animation_base {
    public:
    virtual bool step(); /* return true if continue, false otherwise */
    virtual bool should_run(); /* should we start? */
    virtual ~animation_base();
};

class animation_hook {
    effect_hook_t hook;
    animation_base *anim;
    wayfire_output *output;
    wayfire_view view;

    public:
    animation_hook(animation_base *base, wayfire_output *output, wayfire_view v = nullptr);
    void step();
};

#endif
