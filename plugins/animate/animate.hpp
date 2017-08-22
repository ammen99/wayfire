#ifndef ANIMATE_H_
#define ANIMATE_H_

#include <view.hpp>

class animation_base
{
    public:
    virtual void init(wayfire_view view, int frame_count, bool close);
    virtual bool step(); /* return true if continue, false otherwise */
    virtual ~animation_base();
};

#endif
