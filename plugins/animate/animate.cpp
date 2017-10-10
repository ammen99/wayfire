#include "fire.hpp"
#include <output.hpp>
#include <signal_definitions.hpp>
#include "../../shared/config.hpp"
#include <type_traits>
#include "system_fade.hpp"
#include "basic_animations.hpp"

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
void delete_hook(animation_hook<animation_type, close_animation> *hook)
{
    auto loop = wl_display_get_event_loop(core->ec->wl_display);
    wl_event_loop_add_idle(loop, delete_hook_idle<animation_type, close_animation>, hook);
}

template<class animation_type, bool close_animation>
struct animation_hook
{
    static_assert(std::is_base_of<animation_base, animation_type>::value,
            "animation_type must be derived from animation_base!");

    animation_base *base = nullptr;
    wayfire_view view;
    wayfire_output *output;
    wayfire_grab_interface iface;

    effect_hook_t hook;
    signal_callback_t view_removed;

    bool effect_running = true;
    bool first_run = true;

    animation_hook(wayfire_grab_interface ifc, wayfire_view view, int frame_count) :
        iface(ifc)
    {
        this->view = view;
        output = view->output;

        if (!output->activate_plugin(iface))
        {
            effect_running = false;
            delete_hook(this);
            return;
        }

        /* make sure view is hidden till we actually start the animation */
        if (!close_animation)
            view->transform.color[3] = 0.0;

        if (close_animation)
            view->keep_count++;

        hook = [=] ()
        {
            if (first_run)
            {
                base = static_cast<animation_base*> (new animation_type());
                base->init(view, frame_count, close_animation);
                first_run = false;
            }

            if (!base->step()) {
                delete_hook(this);
                debug << "doing a step " << output->handle->id << " " << view->desktop_surface << std::endl;
            }
debug << "doing a step " << output->handle->id << " " << view->desktop_surface << std::endl;
        };

        output->render->add_output_effect(&hook, view);

        if (!effect_running)
            return;

        view_removed = [=] (signal_data *data)
        {
            auto conv = static_cast<destroy_view_signal*> (data);
            assert(conv);
            if (conv->destroyed_view == view && !close_animation)
                delete_hook(this);
        };

        output->signal->connect_signal("destroy-view", &view_removed);
        output->signal->connect_signal("detach-view", &view_removed);

        output->render->auto_redraw(true);
        debug << "animate: set renderer " << output->handle->id << " " << view->desktop_surface << std::endl;
        output->render->set_renderer();
    }

    ~animation_hook()
    {
        if (!effect_running)
            return;

        if (base)
            delete base;

        output->render->rem_effect(&hook, view);
        output->signal->disconnect_signal("detach-view", &view_removed);
        output->signal->disconnect_signal("destroy-view", &view_removed);

        if (close_animation)
        {
            weston_surface_destroy(view->surface);
            view->handle = nullptr;
        }

        debug << "animate: reset renderer " << output->handle->id << std::endl;
        /* will be false if other animations are still running */
        if (output->deactivate_plugin(iface))
        {
            output->render->auto_redraw(false);
            output->render->reset_renderer();
        }

        /* make sure we "unhide" the view */
        view->transform.color[3] = 1;

        if (close_animation)
            core->erase_view(view);
    }
};

class wayfire_animation : public wayfire_plugin_t {
    signal_callback_t create_cb, destroy_cb, wake_cb;

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
            errio << "[EE] OpenGL ES version below 3.2," <<
                " so no support for Fire effect" <<
                "defaulting to fade effect" << std::endl;
            open_animation = close_animation = "fade";
        }
#endif

        using namespace std::placeholders;
        create_cb = std::bind(std::mem_fn(&wayfire_animation::view_created),
                this, _1);
        destroy_cb = std::bind(std::mem_fn(&wayfire_animation::view_destroyed),
                this, _1);

        wake_cb = [=] (signal_data *data)
        {
            new wf_system_fade(output, startup_duration);
        };

        output->signal->connect_signal("create-view", &create_cb);
        output->signal->connect_signal("destroy-view", &destroy_cb);
        output->signal->connect_signal("wake", &wake_cb);
    }

    /* TODO: enhance - add more animations */
    void view_created(signal_data *ddata)
    {
        create_view_signal *data = static_cast<create_view_signal*> (ddata);
        assert(data);

        if (data->created_view->is_special)
        {
            debug << " got a special view " << data->created_view->output->handle->id << std::endl;
            return;
        }
        if (close_animation != "none")
            data->created_view->surface->ref_count++;

        if (open_animation == "fade")
            new animation_hook<fade_animation, false>(grab_interface, data->created_view, frame_count);
        else if (open_animation == "zoom")
            new animation_hook<zoom_animation, false>(grab_interface, data->created_view, frame_count);
        else if (open_animation == "fire")
            new animation_hook<wf_fire_effect, false>(grab_interface, data->created_view, frame_count);
    }

    void view_destroyed(signal_data *ddata)
    {
        destroy_view_signal *data = static_cast<destroy_view_signal*> (ddata);
        assert(data);

        if (data->destroyed_view->is_special || !data->destroyed_view->destroyed)
            /* this has been a panel or it has been moved to another output, we don't animate it */
            return;

        if (close_animation == "fade")
            new animation_hook<fade_animation, true> (grab_interface, data->destroyed_view, frame_count);
        else if (close_animation == "zoom")
            new animation_hook<zoom_animation, true> (grab_interface, data->destroyed_view, frame_count);
        else if (close_animation == "fire")
            new animation_hook<wf_fire_effect, true> (grab_interface, data->destroyed_view, frame_count);
    }

    void fini()
    {
        output->signal->disconnect_signal("create-view", &create_cb);
        output->signal->disconnect_signal("destroy-view", &destroy_cb);
        output->signal->disconnect_signal("wake", &wake_cb);
    }
    };

extern "C" {
    wayfire_plugin_t *newInstance() {
        return new wayfire_animation();
    }
}
