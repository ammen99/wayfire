#include <plugin.hpp>
#include <output.hpp>
#include <signal-definitions.hpp>
#include <render-manager.hpp>
#include <workspace-manager.hpp>
#include <debug.hpp>
#include <type_traits>
#include <core.hpp>
#include "system_fade.hpp"
#include "basic_animations.hpp"
#include "fire/fire.hpp"

#include "../matcher/matcher.hpp"

void animation_base::init(wayfire_view, int, wf_animation_type) {}
bool animation_base::step() {return false;}
animation_base::~animation_base() {}

/* Represents an animation running for a specific view
 * animation_t is which animation to use (i.e fire, zoom, etc). */
template<class animation_t>
struct animation_hook : public wf::custom_data_t
{
    static_assert(std::is_base_of<animation_base, animation_t>::value,
            "animation_type must be derived from animation_base!");

    static constexpr const char* custom_data_id = "animation-hook";

    wf_animation_type type;
    std::unique_ptr<animation_base> animation;

    wayfire_view view;
    wf::output_t *output;

    /* Update animation right before each frame */
    wf::effect_hook_t update_animation_hook = [=] ()
    {
        view->damage();
        bool result = animation->step();
        view->damage();

        if (!result)
            stop_hook(false);
    };

    /* If the view changes outputs, we need to stop animating, because our animations,
     * hooks, etc are bound to the last output. */
    wf::signal_callback_t view_detached = [=] (wf::signal_data_t *data)
    {
        if (get_signaled_view(data) == view)
            stop_hook(true);
    };

    animation_hook(wayfire_view view, int duration, wf_animation_type type)
    {
        this->type = type;
        this->view = view;
        this->output = view->get_output();

        if (type == ANIMATION_TYPE_UNMAP)
        {
            view->take_ref();
            view->take_snapshot();
        }

        animation = std::make_unique<animation_t> ();
        animation->init(view, duration, type);

        output->render->add_effect(&update_animation_hook, wf::OUTPUT_EFFECT_PRE);

        /* We listen for just the detach-view signal. If the state changes in
         * some other way (i.e view unmapped while map animation), the hook
         * will be overridden by wayfire_animation::set_animation() */
        output->connect_signal("detach-view", &view_detached);
    }

    void stop_hook(bool detached)
    {
        /* We don't want to change the state of the view if it was detached */
        if (type == ANIMATION_TYPE_MINIMIZE && !detached)
            view->set_minimized(true);

        /* Will also delete this */
        view->erase_data(custom_data_id);
    }

    ~animation_hook()
    {
        if (type == ANIMATION_TYPE_UNMAP)
            view->unref();

        output->render->rem_effect(&update_animation_hook);
        output->disconnect_signal("detach-view", &view_detached);
    }
};

class wayfire_animation : public wf::plugin_interface_t
{
    wf::option_wrapper_t<std::string> open_animation{"animate/open_animation"};
    wf::option_wrapper_t<std::string> close_animation{"animate/close_animation"};
    wf::option_wrapper_t<int> duration{"animate/duration"};
    wf::option_wrapper_t<int> startup_duration{"animate/startup_duration"};
    wf::option_wrapper_t<std::string> animation_enabled_for{"animate/enabled_for"};
    wf::option_wrapper_t<std::string> fade_enabled_for{"animate/fade_enabled_for"};
    wf::option_wrapper_t<std::string> zoom_enabled_for{"animate/zoom_enabled_for"};
    wf::option_wrapper_t<std::string> fire_enabled_for{"animate/fire_enabled_for"};

    std::unique_ptr<wf::matcher::view_matcher> animation_enabled_matcher,
        fade_enabled_matcher, zoom_enabled_matcher, fire_enabled_matcher;

    public:
    void init() override
    {
        grab_interface->name = "animate";
        grab_interface->capabilities = 0;

        output->connect_signal("map-view", &on_view_mapped);
        output->connect_signal("pre-unmap-view", &on_view_unmapped);
        output->connect_signal("start-rendering", &on_render_start);
        output->connect_signal("view-minimize-request", &on_minimize_request);

        animation_enabled_matcher =
            wf::matcher::get_matcher(animation_enabled_for.raw_option);
        fade_enabled_matcher = wf::matcher::get_matcher(fade_enabled_for.raw_option);
        zoom_enabled_matcher = wf::matcher::get_matcher(zoom_enabled_for.raw_option);
        fire_enabled_matcher = wf::matcher::get_matcher(fire_enabled_for.raw_option);
    }

    std::string get_animation_for_view(wf::option_wrapper_t<std::string>& anim_type, wayfire_view view)
    {
        /* Determine the animation for the given view.
         * Note that the matcher plugin might not have been loaded, so
         * we need to have a fallback algorithm */
        if (animation_enabled_matcher)
        {
            if (wf::matcher::evaluate(fade_enabled_matcher, view))
                return "fade";
            if (wf::matcher::evaluate(zoom_enabled_matcher, view))
                return "zoom";
            if (wf::matcher::evaluate(fire_enabled_matcher, view))
                return "fire";
            if (wf::matcher::evaluate(animation_enabled_matcher, view))
                return anim_type;
        }
        else if (view->role == wf::VIEW_ROLE_TOPLEVEL ||
            (view->role == wf::VIEW_ROLE_UNMANAGED && view->is_focuseable()))
        {
            return anim_type;
        }

        return "none";
    }

    template<class animation_t>
        void set_animation(wayfire_view view, wf_animation_type type)
    {
        view->store_data(
            std::make_unique<animation_hook<animation_t>> (view, duration, type),
            animation_hook<animation_t>::custom_data_id);
    }

    /* TODO: enhance - add more animations */
    wf::signal_callback_t on_view_mapped =
        [=] (wf::signal_data_t *ddata) -> void
    {
        auto view = get_signaled_view(ddata);
        auto animation = get_animation_for_view(open_animation, view);

        if (animation == "fade")
            set_animation<fade_animation> (view, ANIMATION_TYPE_MAP);
        else if (animation == "zoom")
            set_animation<zoom_animation> (view, ANIMATION_TYPE_MAP);
        else if (animation == "fire")
            set_animation<FireAnimation> (view, ANIMATION_TYPE_MAP);
    };

    wf::signal_callback_t on_view_unmapped = [=] (wf::signal_data_t *data)
    {
        auto view = get_signaled_view(data);
        auto animation = get_animation_for_view(close_animation, view);

        if (animation == "fade")
            set_animation<fade_animation> (view, ANIMATION_TYPE_UNMAP);
        else if (animation == "zoom")
            set_animation<zoom_animation> (view, ANIMATION_TYPE_UNMAP);
        else if (animation == "fire")
            set_animation<FireAnimation> (view, ANIMATION_TYPE_UNMAP);
    };

    wf::signal_callback_t on_minimize_request = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<view_minimize_request_signal*> (data);
        if (ev->state) {
            ev->carried_out = true;
            set_animation<zoom_animation>(ev->view, ANIMATION_TYPE_MINIMIZE);
        } else {
            set_animation<zoom_animation> (ev->view, ANIMATION_TYPE_RESTORE);
        }
    };

    wf::signal_callback_t on_render_start = [=] (wf::signal_data_t *data)
    {
        new wf_system_fade(output, startup_duration);
    };

    void fini() override
    {
        output->disconnect_signal("map-view", &on_view_mapped);
        output->disconnect_signal("pre-unmap-view", &on_view_unmapped);
        output->disconnect_signal("start-rendering", &on_render_start);
        output->disconnect_signal("view-minimize-request", &on_minimize_request);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_animation);
