#ifndef ANIMATE_H_
#define ANIMATE_H_

#include <core.hpp>

class Animation {
    public:
    virtual bool Step(); /* return true if continue, false otherwise */
    virtual bool Run(); /* should we start? */
    virtual ~Animation();
};

class AnimationHook {
    Hook hook;
    Animation *anim;
    public:
    AnimationHook(Animation *_anim);
    void Step();
};

#endif
