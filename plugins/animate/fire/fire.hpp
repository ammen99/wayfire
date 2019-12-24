#ifndef FIRE_ANIMATION_HPP
#define FIRE_ANIMATION_HPP

#include <view-transform.hpp>
#include <render-manager.hpp>
#include <memory>

#include "../animate.hpp"

class FireTransformer;
class FireAnimation : public animation_base
{
    std::string name; // the name of the transformer in the view's table
    wayfire_view view;
    nonstd::observer_ptr<FireTransformer> transformer;
    wf::animation::simple_animation_t progression;

    public:

    ~FireAnimation();
    void init(wayfire_view view, int duration, wf_animation_type type) override;
    bool step() override; /* return true if continue, false otherwise */
};

#endif /* end of include guard: FIRE_ANIMATION_HPP */
