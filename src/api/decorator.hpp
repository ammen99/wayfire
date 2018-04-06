#ifndef DECORATOR_HPP
#define DECORATOR_HPP

#include <view.hpp>

class wf_decorator_frame_t
{
    public:
        /* return the geometry of the contained view relative to the decoration view */
        virtual wf_geometry get_child_geometry(wf_geometry frame_geometry) = 0;
        /* return the geometry of the frame if the child is resized to child_geometry.
         * This must be consistent with get_child_geometry(), e.g
         * get_geometry_interior(get_child_geometry(x)).{width, height} == x.{width, height} */
        virtual wf_geometry get_geometry_interior(wf_geometry child_geometry) = 0;

        virtual ~wf_decorator_frame_t() {};
};

class decorator_base_t
{
    public:
        virtual bool is_decoration_window(std::string title) = 0;
        /* a decoration window has been mapped, it is ready to be set as such */
        virtual void decoration_ready(wayfire_view decor_window) = 0;
};

#endif /* end of include guard: DECORATOR_HPP */
