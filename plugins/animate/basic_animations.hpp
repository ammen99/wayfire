#include "animate.hpp"
#include <plugin.hpp>
#include <opengl.hpp>
#include <view-transform.hpp>
#include <output.hpp>

class fade_animation : public animation_base
{
    wayfire_view view;

    float start = 0, end = 1;
    int total_frames, current_frame;

    public:

    void init(wayfire_view view, int tf, bool close)
    {
        this->view = view;
        total_frames = tf;
        current_frame = 0;

        if (close)
            std::swap(start, end);
    }

    bool step()
    {
        view->alpha = GetProgress(start, end, current_frame, total_frames);
        return current_frame++ < total_frames;
    }

    ~fade_animation()
    {
        view->alpha = 1.0f;

    }
};

class zoom_animation : public animation_base
{
    wayfire_view view;
    wf_2D_view *our_transform = nullptr;

    float alpha_start = 0, alpha_end = 1;
    float zoom_start = 1./3, zoom_end = 1;
    int total_frames, current_frame;

    public:

    void init(wayfire_view view, int tf, bool close)
    {
        this->view = view;
        total_frames = tf;
        current_frame = 0;

        if (close)
        {
            std::swap(alpha_start, alpha_end);
            std::swap(zoom_start, zoom_end);
        }

        auto output = view->get_output();
        GetTuple(sw, sh, output->get_screen_size());

        our_transform = new wf_2D_view(sw, sh);
        view->set_transformer(std::unique_ptr<wf_2D_view> (our_transform));
    }

    bool step()
    {
        float c = GetProgress(zoom_start, zoom_end, current_frame, total_frames);

        auto tr = dynamic_cast<wf_2D_view*> (view->get_transformer());

        tr->alpha = GetProgress(alpha_start, alpha_end, current_frame, total_frames);
        tr->scale_x = c;
        tr->scale_y = c;

        return current_frame++ < total_frames;
    }

    ~zoom_animation()
    {
        if (view->get_transformer() == our_transform)
            view->set_transformer(nullptr);
        view->alpha = 1.0;
    }
};
