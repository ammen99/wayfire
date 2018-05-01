#include <output.hpp>
#include <signal-definitions.hpp>
#include <render-manager.hpp>
#include <debug.hpp>
#include <config.hpp>
#include <type_traits>
#include <core.hpp>
//#include "system_fade.hpp"
#include "basic_animations.hpp"

#if USE_GLES32
#include "fire.hpp"
#endif

void animation_base::init(wayfire_view, int, bool) {}
bool animation_base::step() {return false;}
animation_base::~animation_base() {}

template<class animation_type, bool close_animation>
struct animation_hook;

template<class animation_type, bool close_animation>
void delete_hook_idle(void *data)
{
    auto hook = (animation_hook<animation_type, close_animation>*) data;
    delete hook;
}

template<class animation_type, bool close_animation>
struct animation_hook
{
    static_assert(std::is_base_of<animation_base, animation_type>::value,
            "animation_type must be derived from animation_base!");

    animation_base *base = nullptr;
    wayfire_view view;
    wayfire_output *output;

    effect_hook_t hook;
    signal_callback_t view_removed;

    animation_hook(wayfire_view view, int frame_count)
    {
        log_info("create animation");
        this->view = view;
        output = view->get_output();

        if (close_animation)
            view->take_snapshot();

        view->damage();
        base = dynamic_cast<animation_base*> (new animation_type());
        base->init(view, frame_count, close_animation);
        base->step();
        view->damage();

        hook = [=] ()
        {
            view->damage();
            bool result = base->step();
            view->damage();

            if (!result)
                delete this;
        };

        output->render->add_output_effect(&hook);

        view_removed = [=] (signal_data *data)
        {
            if (get_signaled_view(data) == view && !close_animation)
                delete this;
        };

        output->connect_signal("destroy-view", &view_removed);
        output->connect_signal("detach-view", &view_removed);
    }

    ~animation_hook()
    {
        delete base;

        output->render->rem_effect(&hook);

        output->disconnect_signal("detach-view", &view_removed);
        output->disconnect_signal("destroy-view", &view_removed);

        /* make sure we "unhide" the view */
        view->alpha = 1.0;
        if (close_animation)
            view->dec_keep_count();
    }
};

class wayfire_animation : public wayfire_plugin_t {
    signal_callback_t map_cb, unmap_cb, wake_cb;

    std::string open_animation, close_animation;
    int frame_count;
    int startup_duration;

    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "animate";
        grab_interface->abilities_mask = WF_ABILITY_CUSTOM_RENDERING;

        auto section = config->get_section("animate");
        open_animation = section->get_string("open_animation", "fade");
        close_animation = section->get_string("close_animation", "fade");
        frame_count = section->get_duration("duration", 16);
        startup_duration = section->get_duration("startup_duration", 36);

#if not USE_GLES32
        if(open_animation == "fire" || close_animation == "fire")
        {
            log_error("fire animation not supported (OpenGL ES version < 3.2)");
            open_animation = close_animation = "fade";
        }
#endif

        using namespace std::placeholders;
        map_cb = std::bind(std::mem_fn(&wayfire_animation::view_mapped),
                this, _1);
        unmap_cb = std::bind(std::mem_fn(&wayfire_animation::view_unmapped),
                this, _1);

        wake_cb = [=] (signal_data *data)
        {
//            new wf_system_fade(output, startup_duration);
        };

        output->connect_signal("map-view", &map_cb);
        output->connect_signal("unmap-view", &unmap_cb);

        if (config->get_section("backlight")->get_int("min_brightness", 1) > -1)
            output->connect_signal("wake", &wake_cb);
        output->connect_signal("output-fade-in-request", &wake_cb);
    }

    /* TODO: enhance - add more animations */
    void view_mapped(signal_data *ddata)
    {
        auto view = get_signaled_view(ddata);
        assert(view);

        /* TODO: check if this is really needed */
        if (view->is_special)
            return;

        if (close_animation != "none")
            view->inc_keep_count();

        if (open_animation == "fade")
            new animation_hook<fade_animation, false>(view, frame_count);
        else if (open_animation == "zoom")
            new animation_hook<zoom_animation, false>(view, frame_count);
#if USE_GLES32
        else if (open_animation == "fire")
            new animation_hook<wf_fire_effect, false>(view, frame_count);
#endif
    }

    void view_unmapped(signal_data *data)
    {
        auto view = get_signaled_view(data);

        if (view->is_special)
            return;

        if (close_animation == "fade")
            new animation_hook<fade_animation, true> (view, frame_count);
        else if (close_animation == "zoom")
            new animation_hook<zoom_animation, true> (view, frame_count);
#if USE_GLES32
        else if (close_animation == "fire")
            new animation_hook<wf_fire_effect, true> (view, frame_count);
#endif
    }

    void fini()
    {
        output->disconnect_signal("create-view", &map_cb);
        output->disconnect_signal("destroy-view", &unmap_cb);
        output->disconnect_signal("wake", &wake_cb);
        output->disconnect_signal("output-fade-in-request", &wake_cb);
    }
    };

extern "C" {
    wayfire_plugin_t *newInstance() {
        return new wayfire_animation();
    }
}
