#include "debug.hpp"
#include "output.hpp"
#include "core.hpp"
#include "output-layout.hpp"
#include "workspace-manager.hpp"
#include "render-manager.hpp"
#include "signal-definitions.hpp"
#include "util.hpp"
#include "../output/output-impl.hpp"
#include <xf86drmMode.h>
#include <sstream>
#include <unordered_set>

extern "C"
{
#define static
#include <wlr/backend.h>
#include <wlr/backend/drm.h>
#include <wlr/backend/noop.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/render/wlr_renderer.h>
#undef static
}

static wl_output_transform get_transform_from_string(std::string transform)
{
    if (transform == "normal") return WL_OUTPUT_TRANSFORM_NORMAL;
    else if (transform == "90") return WL_OUTPUT_TRANSFORM_90;
    else if (transform == "180") return WL_OUTPUT_TRANSFORM_180;
    else if (transform == "270") return WL_OUTPUT_TRANSFORM_270;
    else if (transform == "flipped") return WL_OUTPUT_TRANSFORM_FLIPPED;
    else if (transform == "180_flipped") return WL_OUTPUT_TRANSFORM_FLIPPED_180;
    else if (transform == "90_flipped") return WL_OUTPUT_TRANSFORM_FLIPPED_90;
    else if (transform == "270_flipped") return WL_OUTPUT_TRANSFORM_FLIPPED_270;

    log_error ("Bad output transform in config: %s", transform.c_str());
    return WL_OUTPUT_TRANSFORM_NORMAL;
}

std::pair<wlr_output_mode, bool> parse_output_mode(std::string modeline)
{
    wlr_output_mode mode;
    /* If mode refresh is invalid, then it will be autodetected */
    mode.refresh = 0;
    int read = std::sscanf(modeline.c_str(), "%d x %d @ %d",
        &mode.width, &mode.height, &mode.refresh);

    if (mode.refresh < 1000)
        mode.refresh *= 1000;

    if (read < 2 || mode.width <= 0 || mode.height <= 0 || mode.refresh < 0)
        return {mode, false};

    return {mode, true};
}

std::pair<wf_point, bool> parse_output_layout(std::string layout)
{
    wf_point pos;
    int read;

    if (layout.find("@") != layout.npos)
        read = std::sscanf(layout.c_str(), "%d @ %d", &pos.x, &pos.y);
    else
        read = std::sscanf(layout.c_str(), "%d , %d", &pos.x, &pos.y);

    if (read < 2)
    {
        log_error("Detected invalid layout in config: %s", layout.c_str());
        return {{0, 0}, false};
    }

    return {pos, true};
}

wlr_output_mode *find_matching_mode(wlr_output *output,
    const wlr_output_mode& reference)
{
    wlr_output_mode *mode;
    wlr_output_mode *best = NULL;
    wl_list_for_each(mode, &output->modes, link)
    {
        if (mode->width == reference.width && mode->height == reference.height)
        {
            if (mode->refresh == reference.refresh)
                return mode;

            if (!best || best->refresh < mode->refresh)
                best = mode;
        }
    }

    return best;
}

// from rootston
static bool parse_modeline(const char *modeline, drmModeModeInfo &mode)
{
    char hsync[16];
    char vsync[16];
    float fclock;

    mode.type = DRM_MODE_TYPE_USERDEF;

    if (sscanf(modeline, "%f %hd %hd %hd %hd %hd %hd %hd %hd %15s %15s",
               &fclock, &mode.hdisplay, &mode.hsync_start, &mode.hsync_end,
               &mode.htotal, &mode.vdisplay, &mode.vsync_start, &mode.vsync_end,
               &mode.vtotal, hsync, vsync) != 11) {
        return false;
    }

    mode.clock = fclock * 1000;
    mode.vrefresh = mode.clock * 1000.0 * 1000.0 / mode.htotal / mode.vtotal;
    if (strcasecmp(hsync, "+hsync") == 0) {
        mode.flags |= DRM_MODE_FLAG_PHSYNC;
    } else if (strcasecmp(hsync, "-hsync") == 0) {
        mode.flags |= DRM_MODE_FLAG_NHSYNC;
    } else {
        return false;
    }

    if (strcasecmp(vsync, "+vsync") == 0) {
        mode.flags |= DRM_MODE_FLAG_PVSYNC;
    } else if (strcasecmp(vsync, "-vsync") == 0) {
        mode.flags |= DRM_MODE_FLAG_NVSYNC;
    } else {
        return false;
    }

    snprintf(mode.name, sizeof(mode.name), "%dx%d@%d",
             mode.hdisplay, mode.vdisplay, mode.vrefresh / 1000);

    return true;
}

namespace wf
{
    void transfer_views(wf::output_t *from, wf::output_t *to)
    {
        assert(from);

        log_info("transfer views from %s -> %s", from->handle->name,
            to ? to->handle->name : "null");
        /* first move each desktop view(e.g windows) to another output */
        std::vector<wayfire_view> views;
        if (to)
        {
            /* If we aren't moving to another output, then there is no need to
             * enumerate views either */
            views = from->workspace->get_views_in_layer(
                wf::MIDDLE_LAYERS | wf::LAYER_MINIMIZED);
            std::reverse(views.begin(), views.end());
        }

        for (auto& view : views)
            from->workspace->remove_view(view);

        for (auto& view : views)
        {
            view->surface_interface_t::set_output(to);
            to->workspace->add_view(view, view->minimized ?
                wf::LAYER_MINIMIZED : wf::LAYER_WORKSPACE);
            to->workspace->move_to_workspace(view,
                to->workspace->get_current_workspace());
            to->focus_view(view);

            if (view->maximized)
                view->maximize_request(true);

            if (view->fullscreen)
                view->fullscreen_request(to, true);
        }

        /* just remove all other views - backgrounds, panels, etc.
         * desktop views have been removed by the previous cycle */
        for (auto& view : from->workspace->get_views_in_layer(wf::ALL_LAYERS))
        {
            if (view->is_mapped())
                view->close();

            from->workspace->remove_view(view);
            view->set_output(nullptr);
        }
        /* A note: at this point, some views might already have been deleted */
    }

    constexpr wf_point output_state_t::default_position;
    bool output_state_t::operator == (const output_state_t& other) const
    {
        if (source == OUTPUT_IMAGE_SOURCE_NONE)
            return other.source == OUTPUT_IMAGE_SOURCE_NONE;

        if (source == OUTPUT_IMAGE_SOURCE_MIRROR)
        {
            return other.source == OUTPUT_IMAGE_SOURCE_MIRROR &&
                mirror_from == other.mirror_from;
        }

        bool eq = true;

        eq &= source == other.source;
        eq &= position == other.position;
        eq &= (mode.width == other.mode.width);
        eq &= (mode.height == other.mode.height);
        eq &= (mode.refresh == other.mode.refresh);
        eq &= (transform == other.transform);
        eq &= (scale == other.scale);

        return eq;
    }

    /** Represents a single output in the output layout */
    struct output_layout_output_t
    {
        wlr_output *handle;
        output_state_t current_state;

        std::unique_ptr<wf::output_t> output;
        wl_listener_wrapper on_destroy;
        wf_option mode_opt, position_opt, scale_opt, transform_opt;
        const std::string default_value = "default";

        output_layout_output_t(wlr_output *handle)
        {
            this->handle = handle;
            on_destroy.connect(&handle->events.destroy);

            auto output_section =
                wf::get_core().config->get_section(handle->name);
            mode_opt      = output_section->get_option("mode", default_value);
            position_opt  = output_section->get_option("layout", default_value);
            scale_opt     = output_section->get_option("scale", "1");
            transform_opt = output_section->get_option("transform", "normal");
        }

        wlr_output_mode select_default_mode()
        {
            wlr_output_mode *mode;
            wl_list_for_each(mode, &handle->modes, link)
            {
                if (mode->flags & WL_OUTPUT_MODE_PREFERRED)
                    return *mode;
            }

            /* Couldn't find a preferred mode. Just return the last, which is
             * usually also the "largest" */
            wl_list_for_each_reverse(mode, &handle->modes, link)
                return *mode;

            /* Finally, if there isn't any mode (for ex. wayland backend),
             * try the wlr_output resolution, falling back to 1200x720
             * if width or height is <= 0 */
            wlr_output_mode default_mode;
            auto width = handle->width > 0 ? handle->width : 1200;
            auto height = handle->height > 0 ? handle->height : 720;
            auto refresh = handle->refresh > 0 ? handle->refresh : 60000;

            default_mode.width = width;
            default_mode.height = height;
            default_mode.refresh = refresh;

            return default_mode;
        }

        /* Returns true if mode setting for the given output can succeed */
        bool is_mode_supported(wlr_output_mode query)
        {
            /* DRM doesn't support setting a custom mode, so any supported mode
             * must be found in the mode list */
            if (wlr_output_is_drm(handle))
            {
                wlr_output_mode *mode;
                wl_list_for_each(mode, &handle->modes, link)
                {
                    if (mode->width == query.width && mode->height == query.height)
                        return true;
                }

                return false;
            }

            /* X11 and Wayland backends support setting custom modes */
            return true;
        }

        wlr_output_mode load_mode_from_config()
        {
            wlr_output_mode mode;
            mode.width = -1;

            if (mode_opt->as_string() != default_value &&
                mode_opt->as_string() != "auto")
            {
                auto value = parse_output_mode(mode_opt->as_string());
                if (value.second)
                {
                    if (is_mode_supported(value.first)) {
                        mode = value.first;
                    } else {
                        log_error("Output mode %s for output %s is not "
                            " supported, try adding a custom mode.",
                            mode_opt->as_string().c_str(), handle->name);
                    }
                }
            }

            /* Nothing usable in config, try to pick a default mode */
            if (mode.width < 0)
                mode = select_default_mode();

            return mode;
        }

        output_state_t load_state_from_config()
        {
            output_state_t state;

            state.position = output_state_t::default_position;
            if (position_opt->as_string() != default_value)
            {
                auto value = parse_output_layout(position_opt->as_string());
                if (value.second)
                    state.position = value.first;
            }

            /* Make sure we can use custom modes that are
             * specified in the config */
            refresh_custom_modes();

            if (mode_opt->as_string() == "off")
            {
                state.source = OUTPUT_IMAGE_SOURCE_NONE;
                return state;
            }
            else if (mode_opt->as_string().find("mirror") == 0)
            {
                state.source = OUTPUT_IMAGE_SOURCE_MIRROR;

                std::stringstream ss(mode_opt->as_string());
                ss >> state.mirror_from; // skip the mirror word
                ss >> state.mirror_from;

                state.mode = select_default_mode();
            }
            else
            {
                state.source = OUTPUT_IMAGE_SOURCE_SELF;
                state.mode = load_mode_from_config();
            }

            state.scale = scale_opt->as_double();
            if (state.scale <= 0)
            {
                log_error("Invalid scale for %s in config: %s", handle->name,
                    scale_opt->as_string().c_str());
                state.scale = 1;
            }

            state.transform = get_transform_from_string(transform_opt->as_string());
            return state;
        }

        void ensure_wayfire_output()
        {
            if (this->output)
                return;

            this->output = std::make_unique<wf::output_impl_t> (handle);
            auto wo = output.get();

            /* Focus the first output, but do not change the focus on subsequently
             * added outputs. We also change the focus if the noop output was
             * focused */
            wlr_output *focused = get_core().get_active_output() ?
                get_core().get_active_output()->handle : nullptr;
            if (!focused || wlr_output_is_noop(focused))
                get_core().focus_output(wo);

            output_added_signal data;
            data.output = wo;
            get_core().output_layout->emit_signal("output-added", &data);
        }

        void destroy_wayfire_output(bool shutdown)
        {
            if (!this->output)
                return;

            log_info("disable output: %s", output->handle->name);
            auto wo = output.get();

            if (get_core().get_active_output() == wo && !shutdown)
            {
                get_core().focus_output(
                    get_core().output_layout->get_next_output(wo));
            }

            /* It doesn't make sense to transfer to another output if we're
             * going to shut down the compositor */
            transfer_views(wo,
                shutdown ? nullptr : get_core().get_active_output());

            output_removed_signal data;
            data.output = wo;
            get_core().output_layout->emit_signal("output-removed", &data);
            this->output = nullptr;
        }

        std::unordered_set<std::string> added_custom_modes;
        void add_custom_mode(std::string modeline)
        {
            if (added_custom_modes.count(modeline))
                return;

            added_custom_modes.insert(modeline);
            drmModeModeInfo *mode = new drmModeModeInfo;
            if (!parse_modeline(modeline.c_str(), *mode))
            {
                log_error("invalid modeline %s in config file", modeline.c_str());
                return;
            }

            log_debug("output %s: adding custom mode %s", handle->name, mode->name);
            if (wlr_output_is_drm(handle))
                wlr_drm_connector_add_mode(handle, mode);
        }

        void refresh_custom_modes()
        {
            static const std::string custom_mode_prefix = "custom_mode";
            auto section = get_core().config->get_section(handle->name);
            for (auto& opt : section->options)
            {
                if (custom_mode_prefix == opt->name.substr(0, custom_mode_prefix.length()))
                    add_custom_mode(opt->as_string());
            }
        }

        /** Check whether the given state can be applied */
        bool test_state(const output_state_t& state)
        {
            if (state.source == OUTPUT_IMAGE_SOURCE_NONE)
                return true;

            if (state.source == OUTPUT_IMAGE_SOURCE_MIRROR)
                return true;

            /* XXX: are there more things to check? */
            refresh_custom_modes();
            return is_mode_supported(state.mode);
        }

        /** Change the output mode */
        void apply_mode(const wlr_output_mode& mode)
        {
            if (handle->current_mode)
            {
                /* Do not modeset if nothing changed */
                if (handle->current_mode->width == mode.width &&
                    handle->current_mode->height == mode.height &&
                    handle->current_mode->refresh == mode.refresh)
                    return;
            }

            refresh_custom_modes();
            auto built_in = find_matching_mode(handle, mode);
            if (built_in)
            {
                wlr_output_set_mode(handle, built_in);
            } else
            {
                log_info("Couldn't find matching mode %dx%d@%f for output %s."
                    "Trying to use custom mode (might not work).",
                    mode.width, mode.height, mode.refresh / 1000.0, handle->name);
                wlr_output_set_custom_mode(handle, mode.width, mode.height,
                    mode.refresh);
            }

            return;
        }

        /* Mirroring implementation */
        wl_listener_wrapper on_mirrored_frame;
        wl_listener_wrapper on_frame;

        /** Render the output using texture as source */
        void render_output(wlr_texture *texture)
        {
            auto renderer = get_core().renderer;
            wlr_output_attach_render(handle, NULL);
            wlr_renderer_begin(renderer, handle->width, handle->height);

            /* Project a box filling the whole screen */
            float projection[9], box[9];
            wlr_matrix_projection(projection, handle->width, handle->height,
                WL_OUTPUT_TRANSFORM_NORMAL);

            wlr_box geometry = {0, 0, handle->width, handle->height};
            wlr_matrix_project_box(box, &geometry, WL_OUTPUT_TRANSFORM_NORMAL,
                0.0, projection);

            wlr_render_texture_with_matrix(renderer, texture, box, 1.0);
            wlr_renderer_end(renderer);
            wlr_output_commit(handle);
        }

        /* Load output contents and render them */
        void handle_frame()
        {
            auto wo = get_core().output_layout->find_output(
                current_state.mirror_from);
            if (!wo)
            {
                log_error("Cannot find mirrored output %s.",
                    current_state.mirror_from.c_str());
                return;
            }

            wlr_dmabuf_attributes attributes;
            if (!wlr_output_export_dmabuf(wo->handle, &attributes))
            {
                log_error("Failed reading mirrored output contents");
                return;
            }

            /* We export the output to mirror from to a dmabuf, then create
             * a texture from this and use it to render "our" output */
            auto texture = wlr_texture_from_dmabuf(
                get_core().renderer, &attributes);
            render_output(texture);

            wlr_texture_destroy(texture);
            wlr_dmabuf_attributes_finish(&attributes);
        }

        void setup_mirror()
        {
            /* Check if we can mirror */
            auto wo = get_core().output_layout->find_output(
                current_state.mirror_from);

            bool mirror_active = (wo != nullptr);
            if (wo)
            {
                auto config =
                    get_core().output_layout->get_current_configuration();
                auto& wo_state = config[wo->handle];

                if (wo_state.source & OUTPUT_IMAGE_SOURCE_NONE)
                    mirror_active = false;
            }

            if (!mirror_active)
            {
                /* If we mirror from a DPMS or an OFF output, we should turn
                 * off this output as well */
                wlr_output_enable(handle, false);

                log_info("%s: Cannot mirror from output %s. Disabling output.",
                    handle->name, current_state.mirror_from.c_str());
                return;
            }

            wlr_output_schedule_frame(handle);
            on_mirrored_frame.set_callback([=] (void*) {
                /* The mirrored output was repainted, schedule repaint
                 * for us as well */
                wlr_output_schedule_frame(handle);
            });
            on_mirrored_frame.connect(&wo->handle->events.precommit);

            on_frame.set_callback([=] (void*) { handle_frame(); });
            on_frame.connect(&handle->events.frame);
        }

        void teardown_mirror()
        {
            on_mirrored_frame.disconnect();
            on_frame.disconnect();
        }

        /** Apply the given state to the output, ignoring position.
         *
         * This won't have any effect if the output state can't be applied,
         * i.e if test_state(state) == false */
        void apply_state(const output_state_t& state, bool is_shutdown = false)
        {
            if (!test_state(state))
                return;

            this->current_state = state;

            /* Even if output will remain mirrored, we can tear it down and set
             * up again, in case the output to mirror from changed */
            teardown_mirror();
            if (state.source & OUTPUT_IMAGE_SOURCE_NONE)
            {
                /* DPMS or OFF */
                wlr_output_enable(handle, false);
                if (state.source == OUTPUT_IMAGE_SOURCE_NONE)
                {
                    /* OFF */
                    destroy_wayfire_output(is_shutdown);
                    return;
                }
            }
            else
            {
                /* SELF or MIRROR */
                wlr_output_enable(handle, true);
            }

            apply_mode(state.mode);
            if (state.source & OUTPUT_IMAGE_SOURCE_SELF)
            {
                if (handle->transform != state.transform)
                    wlr_output_set_transform(handle, state.transform);

                if (handle->scale != state.scale)
                    wlr_output_set_scale(handle, state.scale);

                ensure_wayfire_output();
                output->render->damage_whole();
                if (!wlr_output_is_noop(handle))
                    output->emit_signal("output-configuration-changed", nullptr);
            }
            else /* state.source == OUTPUT_IMAGE_SOURCE_MIRROR */
            {
                destroy_wayfire_output(is_shutdown);
                setup_mirror();
            }
        }

    };

    class output_layout_t::impl
    {
        std::map<wlr_output*, std::unique_ptr<output_layout_output_t>> outputs;

        wlr_output_layout *output_layout;
        wlr_output_manager_v1 *output_manager;

        wl_listener_wrapper on_new_output;
        wl_listener_wrapper on_output_manager_test;
        wl_listener_wrapper on_output_manager_apply;
        wl_idle_call idle_init_noop;
        wl_idle_call idle_update_configuration;
        wl_timer timer_remove_noop;

        wlr_backend *noop_backend;
        /* Wayfire generally assumes that an enabled output is always available.
         * However, when switching connectors or something it might happen that
         * temporarily no output is available. For those cases, we create a
         * virtual output with the noop backend. */
        std::unique_ptr<output_layout_output_t> noop_output;

        bool shutdown_received = false;
        signal_callback_t on_config_reload, on_shutdown;

        public:
        impl(wlr_backend *backend)
        {
            on_new_output.set_callback([=] (void *data) {
                add_output((wlr_output*) data);
            });
            on_new_output.connect(&backend->events.new_output);

            output_layout = wlr_output_layout_create();

            on_config_reload = [=] (void*) { reconfigure_from_config(); };
            get_core().connect_signal("reload-config", &on_config_reload);
            on_shutdown = [=] (void*) {
                shutdown_received = true;
            };
            get_core().connect_signal("shutdown", &on_shutdown);

            noop_backend = wlr_noop_backend_create(get_core().display);
            /* The noop output will be typically destroyed on the first
             * plugged monitor, however we need to create it here so that we
             * support booting with 0 monitors */
            idle_init_noop.run_once([&] () {
                if (get_outputs().empty())
                    ensure_noop_output();
            });

            output_manager = wlr_output_manager_v1_create(get_core().display);
            on_output_manager_test.set_callback([=] (void *data) {
                apply_wlr_configuration((wlr_output_configuration_v1*) data, true);
            });
            on_output_manager_apply.set_callback([=] (void *data) {
                apply_wlr_configuration((wlr_output_configuration_v1*) data, false);
            });

            on_output_manager_test.connect(&output_manager->events.test);
            on_output_manager_apply.connect(&output_manager->events.apply);
        }

        ~impl()
        {
            if (noop_output)
                noop_output->destroy_wayfire_output(true);
            wlr_backend_destroy(noop_backend);

            get_core().disconnect_signal("reload-config", &on_config_reload);
        }

        output_configuration_t output_configuration_from_wlr_configuration(
            wlr_output_configuration_v1 *configuration)
        {
            output_configuration_t result;
            wlr_output_configuration_head_v1 *head;
            wl_list_for_each(head, &configuration->heads, link)
            {
                if (!this->outputs.count(head->state.output))
                {
                    log_error("Output configuration request contains unknown"
                        " output, probably a compositor bug!");
                    continue;
                }

                auto& handle = head->state.output;
                auto& state = result[handle];

                if (!head->state.enabled)
                {
                    state.source = OUTPUT_IMAGE_SOURCE_NONE;
                    continue;
                }

                state.source = OUTPUT_IMAGE_SOURCE_SELF;
                state.mode = head->state.mode ? *head->state.mode :
                    this->outputs[handle]->current_state.mode;
                state.position = {head->state.x, head->state.y};
                state.scale = head->state.scale;
                state.transform = head->state.transform;
            }

            return result;
        }

        void apply_wlr_configuration(
            wlr_output_configuration_v1 *wlr_configuration, bool test_only)
        {
            auto configuration =
                output_configuration_from_wlr_configuration(wlr_configuration);

            if (apply_configuration(configuration, test_only)) {
                wlr_output_configuration_v1_send_succeeded(wlr_configuration);
            } else {
                wlr_output_configuration_v1_send_failed(wlr_configuration);
            }
        }

        void ensure_noop_output()
        {
            log_info("new output: NOOP-1");

            if (!noop_output)
            {
                auto handle = wlr_noop_add_output(noop_backend);
                noop_output = std::make_unique<output_layout_output_t> (handle);
            }

            /* Make sure that the noop output is up and running even before the
             * next reconfiguration. This is needed because if we are removing
             * an output, we might get into a situation where the last physical
             * output has already been removed but we are yet to add the noop one */
            noop_output->apply_state(noop_output->load_state_from_config());
            wlr_output_layout_add_auto(output_layout, noop_output->handle);
        }

        void remove_noop_output()
        {
            if (!noop_output)
                return;

            if (noop_output->current_state.source == OUTPUT_IMAGE_SOURCE_NONE)
                return;

            log_info("remove output: NOOP-1");

            output_state_t state;
            state.source = OUTPUT_IMAGE_SOURCE_NONE;
            noop_output->apply_state(state);
            wlr_output_layout_remove(output_layout, noop_output->handle);
        }

        void add_output(wlr_output *output)
        {
            log_info("new output: %s", output->name);

            auto lo = new output_layout_output_t(output);
            outputs[output] = std::unique_ptr<output_layout_output_t>(lo);
            lo->on_destroy.set_callback([output, this] (void*) {
                remove_output(output);
            });

            reconfigure_from_config();
        }

        void remove_output(wlr_output *to_remove)
        {
            auto active_outputs = get_outputs();
            log_info("remove output: %s", to_remove->name);

            /* Unset mode, plus destroy the wayfire output */
            auto configuration = get_current_configuration();
            configuration[to_remove].source = OUTPUT_IMAGE_SOURCE_NONE;
            apply_configuration(configuration);

            outputs.erase(to_remove);

            /* If no physical outputs, then at least the noop output */
            assert(get_outputs().size() || shutdown_received);
        }

        /* Get the current configuration of all outputs */
        output_configuration_t get_current_configuration()
        {
            output_configuration_t configuration;
            for (auto& entry : this->outputs)
                configuration[entry.first] = entry.second->current_state;

            return configuration;
        }

        output_configuration_t last_config_configuration;

        /** Load config from file, test and apply */
        void reconfigure_from_config()
        {
            /* Load from config file */
            output_configuration_t configuration;
            for (auto& entry : this->outputs)
                configuration[entry.first] = entry.second->load_state_from_config();

            if (configuration == get_current_configuration() ||
                configuration == last_config_configuration)
            {
                return;
            }

            if (test_configuration(configuration))
                apply_configuration(configuration);
        }

        /** Check whether the given configuration can be applied */
        bool test_configuration(const output_configuration_t& config)
        {
            if (config.size() != this->outputs.size())
                return false;

            bool ok = true;
            for (auto& entry : config)
            {
                if (this->outputs.count(entry.first) == 0)
                    return false;

                ok &= this->outputs[entry.first]->test_state(entry.second);
            }

            /* TODO: reject overlapping outputs */
            /* TODO: possibly reject disjoint outputs? Note: this doesn't apply
             * if an output was destroyed */
            /* TODO: reject no enabled output setups */
            /* TODO: reject bad mirror configurations */
            return ok;
        }

        /** Apply the given configuration. Config MUST be a valid configuration */
        void apply_configuration(const output_configuration_t& config)
        {
            /* The order in which we enable and disable outputs is important.
             * Firstly, on some systems where there aren't enough CRTCs, we can
             * only enable a subset of all outputs at once. This means we should
             * first try to disable as many outputs as possible, and only then
             * start enabling new ones.
             *
             * Secondly, we need to check when we need to enable noop output -
             * which is exactly when all currently enabled outputs are going to
             * be disabled */

            /* Number of outputs that were enabled and continue to be enabled */
            int count_remaining_enabled = 0;
            auto active_outputs = get_outputs();
            for (auto& wo : active_outputs)
            {
                auto it = config.find(wo->handle);
                if (it != config.end() &&
                    (it->second.source & OUTPUT_IMAGE_SOURCE_SELF))
                {
                    ++count_remaining_enabled;
                }
            }

            bool turning_off_all_active =
                !active_outputs.empty() && count_remaining_enabled == 0;
            bool is_noop_active = noop_output && noop_output->output;

            if (turning_off_all_active && !shutdown_received && !is_noop_active)
            {
                /* If we aren't shutting down, and we will turn off all the
                 * currently enabled outputs, we'll need the noop output, as a
                 * temporary output to store views in, until a real output is
                 * enabled again */
                ensure_noop_output();
            }

            /* First: disable all outputs that need disabling */
            for (auto& entry : config)
            {
                auto& handle = entry.first;
                auto& state = entry.second;
                auto& lo = this->outputs[handle];

                if (!(state.source & OUTPUT_IMAGE_SOURCE_SELF))
                {
                    /* First shut down the output, move its views, etc. while it
                     * is still in the output layout and its global is active.
                     *
                     * This is needed so that clients can receive
                     * wl_surface.leave events for the to be destroyed output */
                    lo->apply_state(state, shutdown_received);
                    wlr_output_layout_remove(output_layout, handle);
                }
            }

            /* Second: enable outputs */
            int count_enabled = 0;
            for (auto& entry : config)
            {
                auto& handle = entry.first;
                auto& state = entry.second;
                auto& lo = this->outputs[handle];

                if (state.source & OUTPUT_IMAGE_SOURCE_SELF)
                {
                    ++count_enabled;
                    if (entry.second.position != output_state_t::default_position) {
                        wlr_output_layout_add(output_layout, handle,
                            state.position.x, state.position.y);
                    } else {
                        wlr_output_layout_add_auto(output_layout, handle);
                    }

                    lo->apply_state(state, shutdown_received);
                }
            }

            /* Third: enable mirrored outputs */
            for (auto& entry : config)
            {
                auto& handle = entry.first;
                auto& state = entry.second;
                auto& lo = this->outputs[handle];

                if (state.source == OUTPUT_IMAGE_SOURCE_MIRROR)
                {
                    lo->apply_state(state, shutdown_received);
                    wlr_output_layout_remove(output_layout, handle);
                }
            }

            get_core().output_layout->emit_signal("configuration-changed", nullptr);

            if (count_enabled > 0)
            {
                /* Make sure to remove the noop output if it is no longer needed.
                 * NB: Libwayland has a bug when a global is created and
                 * immediately destroyed, as clients don't have enough time
                 * to bind it. That's why we don't destroy noop immediately,
                 * but only after a timeout */
                timer_remove_noop.set_timeout(1000, [=] () {
                    remove_noop_output();
                });
            }

            idle_update_configuration.run_once([=] () {
                send_wlr_configuration();
            });
        }

        void send_wlr_configuration()
        {
            auto wlr_configuration = wlr_output_configuration_v1_create();
            for (auto& output : outputs)
            {
                auto head = wlr_output_configuration_head_v1_create(
                    wlr_configuration, output.first);

                auto box = wlr_output_layout_get_box(output_layout, output.first);
                if (box)
                {
                    head->state.x = box->x;
                    head->state.y = box->y;
                }
            }

            wlr_output_manager_v1_set_configuration(output_manager,
                wlr_configuration);
        }

        /* Public API functions */
        wlr_output_layout *get_handle() { return output_layout; }
        size_t get_num_outputs() { return get_outputs().size(); }

        wf::output_t *find_output(wlr_output *output)
        {
            if (outputs.count(output))
                return outputs[output]->output.get();

            if (noop_output && noop_output->handle == output)
                return noop_output->output.get();

            return nullptr;
        }

        wf::output_t *find_output(std::string name)
        {
            for (auto& entry : outputs)
            {
                if (entry.first->name == name)
                    return entry.second->output.get();
            }

            if (noop_output && noop_output->handle->name == name)
                return noop_output->output.get();

            return nullptr;
        }

        std::vector<wf::output_t*> get_outputs()
        {
            std::vector<wf::output_t*> result;
            for (auto& entry : outputs)
            {
                if (entry.second->current_state.source & OUTPUT_IMAGE_SOURCE_SELF)
                    result.push_back(entry.second->output.get());
            }

            if (result.empty())
            {
                if (noop_output && noop_output->output)
                    result.push_back(noop_output->output.get());
            }

            return result;
        }

        wf::output_t *get_next_output(wf::output_t *output)
        {
            auto os = get_outputs();

            auto it = std::find(os.begin(), os.end(), output);
            if (it == os.end() || std::next(it) == os.end()) {
                return os[0];
            } else {
                return *(++it);
            }
        }

        wf::output_t *get_output_coords_at(int x, int y, int& rx, int& ry)
        {
            double lx = x, ly = y;
            wlr_output_layout_closest_point(output_layout, NULL, lx, ly, &lx, &ly);
            auto handle = wlr_output_layout_output_at(output_layout, lx, ly);

            assert(handle || shutdown_received);
            if (!handle)
                return nullptr;

            rx = lx;
            ry = ly;

            if (noop_output && handle == noop_output->handle) {
                return noop_output->output.get();
            } else {
                return outputs[handle]->output.get();
            }
        }

        wf::output_t *get_output_at(int x, int y)
        {
            int dummy_x, dummy_y;
            return get_output_coords_at(x, y, dummy_x, dummy_y);
        }

        bool apply_configuration(const output_configuration_t& configuration,
            bool test_only)
        {
            bool ok = test_configuration(configuration);
            if (ok && !test_only)
                apply_configuration(configuration);

            return ok;
        }
    };

    /* Just pass to the PIMPL */
    output_layout_t::output_layout_t(wlr_backend *b) : pimpl(new impl(b)) {}
    output_layout_t::~output_layout_t() = default;
    wlr_output_layout *output_layout_t::get_handle()
    { return pimpl->get_handle(); }
    wf::output_t *output_layout_t::get_output_at(int x, int y)
    { return pimpl->get_output_at(x, y); }
    wf::output_t *output_layout_t::get_output_coords_at(int x, int y, int& lx, int& ly)
    { return pimpl->get_output_coords_at(x, y, lx, ly); }
    size_t output_layout_t::get_num_outputs()
    { return pimpl->get_num_outputs(); }
    std::vector<wf::output_t*> output_layout_t::get_outputs()
    { return pimpl->get_outputs(); }
    wf::output_t *output_layout_t::get_next_output(wf::output_t *output)
    { return pimpl->get_next_output(output); }
    wf::output_t *output_layout_t::find_output(wlr_output *output)
    { return pimpl->find_output(output); }
    wf::output_t *output_layout_t::find_output(std::string name)
    { return pimpl->find_output(name); }
    output_configuration_t output_layout_t::get_current_configuration()
    { return pimpl->get_current_configuration(); }
    bool output_layout_t::apply_configuration(const output_configuration_t& configuration, bool test_only)
    { return pimpl->apply_configuration(configuration, test_only); }
}
