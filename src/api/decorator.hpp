#ifndef DECORATOR_HPP
#define DECORATOR_HPP

#include <view.hpp>

class wf_decorator_frame_t
{
  public:
    virtual wf_geometry expand_wm_geometry(wf_geometry contained_wm_geometry) = 0;
    virtual void calculate_resize_size(int& target_width, int& target_height) = 0;

    virtual void notify_view_activated(bool active) {}
    virtual void notify_view_resized(wf_geometry view_geometry) {}
    virtual void notify_view_maximized() {}
    virtual void notify_view_fullscreened() {}

    virtual ~wf_decorator_frame_t() {}
};

#endif /* end of include guard: DECORATOR_HPP */
