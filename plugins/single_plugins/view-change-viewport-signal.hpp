#ifndef VIEW_CHANGE_VIEWPORT_CPP
#define VIEW_CHANGE_VIEWPORT_CPP

#include <output.hpp>

struct view_change_viewport_signal : public signal_data
{
    wayfire_view view;
    std::tuple<int, int> from, to;
};

#endif /* end of include guard: VIEW_CHANGE_VIEWPORT_CPP */
