#ifndef FIRE_H_
#define FIRE_H_

#include "animate.hpp"
#include "particle.hpp"
#include <output.hpp>

class fire_particle_system;

class wf_fire_effect : public animation_base
{
    fire_particle_system *ps;
    wayfire_view w;

    weston_geometry last_geometry;

    int progress = 0, effect_cycles;
    bool burnout;

    void adjust_alpha();

    public:
        void init(wayfire_view win, int fr_cnt, bool burnout);
        bool step();
        ~wf_fire_effect();
};

#endif
