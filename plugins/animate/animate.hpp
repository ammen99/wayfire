#ifndef ANIMATE_H_
#define ANIMATE_H_

#include <core.hpp>

class Animation {
    public:
    virtual bool step(); /* return true if continue, false otherwise */
    virtual bool run(); /* should we start? */
    virtual ~Animation();
};

class AnimationHook {
    EffectHook hook;
    Animation *anim;
    Output *output;

    public:
    AnimationHook(Animation *_anim, Output *output, View v = nullptr);
    void step();
};

#endif
