#include "animate.hpp"
#include <plugin.hpp>
#include <opengl.hpp>

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
        view->transform.color[3] = GetProgress(start, end, current_frame, total_frames);
        view->simple_render();
        view->transform.color[3] = 0.0f;

        return current_frame++ < total_frames;
    }

    ~fade_animation()
    {
        view->transform.color[3] = 1.0f;
    }
};

class zoom_animation : public animation_base
{
    wayfire_view view;

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

    }

    bool step()
    {
        view->transform.color[3] = GetProgress(alpha_start, alpha_end, current_frame, total_frames);

        float c = GetProgress(zoom_start, zoom_end, current_frame, total_frames);

        auto og = view->output->get_full_geometry();

        int cx = view->geometry.x + view->geometry.width  / 2 - og.x;
        int cy = view->geometry.y + view->geometry.height / 2 - og.y;

        float tx = (cx - og.width / 2 ) * 2. / og.width;
        float ty = (og.height / 2 - cy) * 2. / og.height;

        view->transform.translation = glm::translate(glm::mat4(),
                {tx, ty, 0});

        view->transform.scale = glm::scale(glm::mat4(), {c, c, 1});

        auto compositor_geometry = view->geometry;

        view->geometry.x = og.x + og.width  / 2 - view->geometry.width / 2;
        view->geometry.y = og.y + og.height / 2 - view->geometry.height / 2;

        view->simple_render();
        view->transform.color[3] = 0.0f;

        view->geometry = compositor_geometry;

        return current_frame++ < total_frames;
    }

    ~zoom_animation()
    {
        view->transform.color[3] = 1.0f;
        view->transform.scale = glm::mat4();
        view->transform.translation = glm::mat4();
    }
};
