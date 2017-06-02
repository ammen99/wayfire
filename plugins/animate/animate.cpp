//#include "fire.hpp"
//#include <opengl.hpp>
#include "animate.hpp"
#include <output.hpp>
#include <signal_definitions.hpp>
#include "../../shared/config.hpp"


/* TODO: kein compute shader
#define HAS_COMPUTE_SHADER (OpenGL::VersionMajor > 4 ||  \
        (OpenGL::VersionMajor == 4 && OpenGL::VersionMinor >= 3))
        */

bool animation_base::step() {return false;}
bool animation_base::should_run() {return true;}
animation_base::~animation_base() {}

animation_hook::animation_hook(animation_base *anim,
        wayfire_output *output, wayfire_view v)
{
    this->anim = anim;
    this->output = output;

    if (anim->should_run()) {
        hook = std::bind(std::mem_fn(&animation_hook::step), this);
        this->view = v;

        output->render->add_output_effect(&hook, view);
    } else {
        delete anim;
        delete this;
    }
}

void animation_hook::step()
{
    if (!this->anim->step()) {
        output->render->rem_effect(&hook, view);
        delete anim;
        delete this;
    }
}

void fade_out_done_idle_cb(void *data)
{
    weston_view *view = (weston_view*) data;;
    if (view) {
        weston_surface_destroy(view->surface);
        weston_layer_entry_remove(&view->layer_link);
    }
}

void fade_out_animation_cb(weston_view_animation*, void *data)
{
    auto loop = wl_display_get_event_loop(core->ec->wl_display);
    auto view = (weston_view*) data;
    if (weston_view_is_mapped(view)) {
        view->is_mapped = false;
        wl_event_loop_add_idle(loop, fade_out_done_idle_cb, view);
    }
}

class fade_animation : public animation_base {
    wayfire_view view;
    bool rev;
    public:
    fade_animation(wayfire_view v, bool reverse)
    {
        view = v;
        rev = reverse;
    }

    bool should_run()
    {
        if (!rev) {
            weston_fade_run(view->handle, 0, 1, 200, NULL, NULL);
        } else {
            debug << "Fade animation started" << std::endl;
            if (weston_surface_is_mapped(view->surface)) {
                debug << "surface is mapped" << std::endl;
                pixman_region32_fini(&view->surface->pending.input);
                pixman_region32_init(&view->surface->pending.input);
                pixman_region32_fini(&view->surface->input);
                pixman_region32_init(&view->surface->input);

                weston_fade_run(view->handle, 1.0, 0.0, 200.0, fade_out_animation_cb,
                        view->handle);
            } else {
                debug << "surface ain't mapped" << std::endl;
                weston_view_destroy(view->handle);
            }
        }
        return false;
    }
};

class wayfire_animation : public wayfire_plugin_t {
    signal_callback_t create_cb, destroy_cb;

    std::string open_animation, close_animation;

    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "animate";
        grab_interface->compatAll = false;

        auto section = config->get_section("animate");
        open_animation = section->get_string("open_animation", "fade");
        close_animation = section->get_string("close_animation", "fade");

#if false
        if(map_animation == "fire") {
            if(!HAS_COMPUTE_SHADER) {
                error << "[EE] OpenGL version below 4.3," <<
                    " so no support for Fire effect" <<
                    "defaulting to fade effect" << std::endl;
                map_animation = "fade";
            }
#endif

        using namespace std::placeholders;
        create_cb = std::bind(std::mem_fn(&wayfire_animation::view_created),
                this, _1);
        destroy_cb = std::bind(std::mem_fn(&wayfire_animation::view_destroyed),
                this, _1);

        output->signal->connect_signal("create-view", &create_cb);
        output->signal->connect_signal("destroy-view", &destroy_cb);
    }

    /* TODO: enhance - add more animations */
    void view_created(signal_data *ddata)
    {
        create_view_signal *data = static_cast<create_view_signal*> (ddata);
        assert(data);

        if (data->created_view->is_special)
            return;

        data->created_view->surface->ref_count++;
        if (open_animation == "fade")
            new animation_hook(new fade_animation(data->created_view, false), output);
    }

    void view_destroyed(signal_data *ddata)
    {
        destroy_view_signal *data = static_cast<destroy_view_signal*> (ddata);
        assert(data);

        if (data->destroyed_view->is_special || !data->destroyed_view->destroyed)
            /* this has been a panel or it has been moved to another output, we don't animate it */
            return;

        if (open_animation == "fade") {
            data->destroyed_view->keep_count++;
            new animation_hook(new fade_animation(data->destroyed_view, true), output);
        }
    }

    void fini() {
        output->signal->disconnect_signal("create-view", &create_cb);
        output->signal->disconnect_signal("destroy-view", &destroy_cb);
    }
    };

extern "C" {
    wayfire_plugin_t *newInstance() {
        return new wayfire_animation();
    }
}
