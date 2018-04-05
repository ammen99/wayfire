#ifndef DECORATOR_HPP
#define DECORATOR_HPP

#include <view.hpp>

class wf_decorator_frame_t
{
    public:
        /* return the size of the framed view,
         * if the window (framed view + frame) has geometry base */
        virtual wf_geometry get_child_geometry(wf_geometry base) = 0;
        virtual ~wf_decorator_frame_t() {};
};

class decorator_base_t
{
    public:
        virtual bool is_decoration_window(std::string title) = 0;
        /* a decoration window has been mapped, it is ready to be set as such */
        virtual void decoration_ready(std::unique_ptr<wayfire_view_t> decor_window) = 0;
};

#endif /* end of include guard: DECORATOR_HPP */
