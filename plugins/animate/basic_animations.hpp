#include "animate.hpp"
#include <debug.hpp>
#include <plugin.hpp>
#include <opengl.hpp>
#include <view-transform.hpp>
#include <output.hpp>

class fade_animation : public animation_base
{
    wayfire_view view;

    float start = 0, end = 1;
    wf_duration duration;
    std::string name;

    public:

    void init(wayfire_view view, wf_option dur, wf_animation_type type)
    {
        this->view = view;
        duration = wf_duration(dur);
        duration.start();

        if (type & HIDING_ANIMATION)
            std::swap(start, end);

        name = "animation-fade-" + std::to_string(type);
        view->add_transformer(std::make_unique<wf_2D_view> (view), name);
    }

    bool step()
    {
        auto transform = dynamic_cast<wf_2D_view*> (view->get_transformer(name).get());
        transform->alpha = duration.progress(start, end);
        return duration.running();
    }

    ~fade_animation()
    {
        view->pop_transformer(name);
    }
};

class zoom_animation : public animation_base
{
    wayfire_view view;
    wf_2D_view *our_transform = nullptr;

    wf_transition alpha {0, 1}, zoom {1./3, 1},
                  offset_x{0, 0}, offset_y{0, 0};
    wf_duration duration;

    public:

    void init(wayfire_view view, wf_option dur, wf_animation_type type)
    {
        this->view = view;
        duration = wf_duration(dur);
        duration.start();

        if (type & MINIMIZE_STATE_ANIMATION)
        {
            auto hint = view->get_minimize_hint();
            if (hint.width > 0 && hint.height > 0)
            {
                int hint_cx = hint.x + hint.width / 2;
                int hint_cy = hint.y + hint.height / 2;

                auto bbox = view->get_wm_geometry();
                int view_cx = bbox.x + bbox.width / 2;
                int view_cy = bbox.y + bbox.height / 2;

                offset_x = {1.0 * hint_cx - view_cx, 0};
                offset_y = {1.0 * hint_cy - view_cy, 0};

                if (bbox.width > 0 && bbox.height > 0)
                {
                    double scale_x = 1.0 * hint.width / bbox.width;
                    double scale_y = 1.0 * hint.height / bbox.height;
                    zoom = {std::min(scale_x, scale_y), 1};
                }
            }
        }

        if (type & HIDING_ANIMATION)
        {
            std::swap(alpha.start, alpha.end);
            std::swap(zoom.start, zoom.end);
            std::swap(offset_x.start, offset_x.end);
            std::swap(offset_y.start, offset_y.end);
        }

        our_transform = new wf_2D_view(view);
        view->add_transformer(std::unique_ptr<wf_2D_view> (our_transform));
    }

    bool step()
    {
        float c = duration.progress(zoom);

        our_transform->alpha = duration.progress(alpha);
        our_transform->scale_x = c;
        our_transform->scale_y = c;

        our_transform->translation_x = duration.progress(offset_x);
        our_transform->translation_y = duration.progress(offset_y);

        return duration.running();
    }

    ~zoom_animation()
    {
        view->pop_transformer(nonstd::make_observer(our_transform));
    }
};
