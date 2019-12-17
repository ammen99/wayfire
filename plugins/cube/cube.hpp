#ifndef WF_CUBE_HPP
#define WF_CUBE_HPP

#include <config.h>
#include <wayfire/util/duration.hpp>
#include <opengl.hpp>

#define TEX_ERROR_FLAG_COLOR  0, 1, 0, 1

struct wf_cube_animation_attribs
{
    wf_duration duration;

    glm::mat4 projection, view;
    float side_angle;

    wf_transition offset_y {0, 0}, offset_z {0, 0}, rotation {0, 0}, zoom{1, 1};
    wf_transition ease_deformation{0, 0}; // used only with tesselation enabled

    bool in_exit;
};

#endif /* end of include guard: WF_CUBE_HPP */
