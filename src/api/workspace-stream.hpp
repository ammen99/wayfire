#ifndef WF_WORKSPACE_STREAM_HPP
#define WF_WORKSPACE_STREAM_HPP

#include "opengl.hpp"
#include "object.hpp"

namespace wf
{
/** A workspace stream is a way for plugins to obtain the contents of a
 * given workspace.  */
struct workspace_stream_t
{
    std::tuple<int, int> ws;
    wf_framebuffer_base buffer;
    bool running = false;

    float scale_x = 1.0;
    float scale_y = 1.0;

    /* The background color of the stream, when there is no view above it */
    wf_color background = {0.0f, 0.0f, 0.0f, 1.0f};
};

/** Emitted whenever a workspace stream is being started or stopped */
struct stream_signal_t : public wf::signal_data_t
{
    stream_signal_t(wf_region& damage, const wf_framebuffer& _fb)
        : raw_damage(damage), fb(_fb) { }

    /* Raw damage, can be adjusted by the signal handlers. */
    wf_region& raw_damage;
    const wf_framebuffer& fb;
};
}

#endif /* end of include guard: WF_WORKSPACE_STREAM_HPP */

