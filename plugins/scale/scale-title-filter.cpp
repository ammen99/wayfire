#include <cctype>
#include <string>
#include <map>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/plugins/scale-signal.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

#include <linux/input-event-codes.h>

#include <wayfire/render-manager.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>
#include <wayfire/plugins/common/simple-texture.hpp>

struct scale_key_repeat_t
{
    wf::option_wrapper_t<int> delay{"input/kb_repeat_delay"};
    wf::option_wrapper_t<int> rate{"input/kb_repeat_rate"};

    wf::wl_timer timer_delay;
    wf::wl_timer timer_rate;

    using callback_t = std::function<void (uint32_t)>;

    scale_key_repeat_t(uint32_t key, callback_t handler)
    {
        timer_delay.set_timeout(delay, [=] ()
        {
            timer_rate.set_timeout(1000 / rate, [=] ()
            {
                handler(key);
                return true; // repeat
            });

            return false; // no more repeat
        });
    }
};

class scale_title_filter : public wf::plugin_interface_t
{
    wf::option_wrapper_t<bool> case_sensitive{"scale-title-filter/case_sensitive"};
    std::string title_filter;
    /* since title filter is utf-8, here we store the length of each
     * character when adding them so backspace will work properly */
    std::vector<int> char_len;
    bool scale_running = false;

    inline void fix_case(std::string& string)
    {
        if (case_sensitive)
        {
            return;
        }

        auto transform = [] (unsigned char c) -> unsigned char
        {
            if (std::isspace(c))
            {
                return ' ';
            }

            return (c <= 127) ? (unsigned char)std::tolower(c) : c;
        };
        std::transform(string.begin(), string.end(), string.begin(), transform);
    }

    bool should_show_view(wayfire_view view)
    {
        if (title_filter.empty())
        {
            return true;
        }

        auto title  = view->get_title();
        auto app_id = view->get_app_id();
        auto filter = title_filter;

        fix_case(title);
        fix_case(app_id);
        fix_case(filter);

        return (title.find(filter) != std::string::npos) ||
               (app_id.find(filter) != std::string::npos);
    }

  public:
    void init() override
    {
        grab_interface->name = "scale-title-filter";
        grab_interface->capabilities = 0;

        output->connect_signal("scale-filter", &view_filter);
        output->connect_signal("scale-end", &scale_end);
    }

    void fini() override
    {
        clear_overlay();
        output->disconnect_signal(&view_filter);
        wf::get_core().disconnect_signal(&scale_key);
        output->disconnect_signal(&scale_end);
    }

    wf::signal_connection_t view_filter{[this] (wf::signal_data_t *data)
        {
            if (!scale_running)
            {
                wf::get_core().connect_signal("keyboard_key", &scale_key);
                scale_running = true;
            }

            auto signal = static_cast<scale_filter_signal*>(data);
            scale_filter_views(signal, [this] (wayfire_view v)
            {
                return !should_show_view(v);
            });
        }
    };

    std::map<uint32_t, std::unique_ptr<scale_key_repeat_t>> keys;
    scale_key_repeat_t::callback_t handle_key_repeat = [=] (uint32_t raw_keycode)
    {
        auto seat     = wf::get_core().get_current_seat();
        auto keyboard = wlr_seat_get_keyboard(seat);
        if (!keyboard)
        {
            return; /* should not happen */
        }

        auto xkb_state = keyboard->xkb_state;
        xkb_keycode_t keycode = raw_keycode + 8;
        xkb_keysym_t keysym   = xkb_state_key_get_one_sym(xkb_state, keycode);
        if (keysym == XKB_KEY_BackSpace)
        {
            if (!title_filter.empty())
            {
                int len = char_len.back();
                char_len.pop_back();
                title_filter.resize(title_filter.length() - len);
            } else
            {
                return;
            }
        } else
        {
            /* taken from libxkbcommon guide */
            int size = xkb_state_key_get_utf8(xkb_state, keycode, nullptr, 0);
            if (size <= 0)
            {
                return;
            }

            std::string tmp(size, 0);
            xkb_state_key_get_utf8(xkb_state, keycode, tmp.data(), size + 1);
            char_len.push_back(size);
            title_filter += tmp;
        }

        output->emit_signal("scale-update", nullptr);
        update_overlay();
    };

    wf::signal_connection_t scale_key = [this] (wf::signal_data_t *data)
    {
        auto k =
            static_cast<wf::input_event_signal<wlr_event_keyboard_key>*>(data);
        if (k->event->state == WL_KEYBOARD_KEY_STATE_RELEASED)
        {
            keys.erase(k->event->keycode);
            return;
        }

        if ((k->event->keycode == KEY_ESC) || (k->event->keycode == KEY_ENTER))
        {
            return;
        }

        keys[k->event->keycode] =
            std::make_unique<scale_key_repeat_t>(k->event->keycode,
                handle_key_repeat);
        handle_key_repeat(k->event->keycode);
    };


    wf::signal_connection_t scale_end = [this] (wf::signal_data_t*)
    {
        wf::get_core().disconnect_signal(&scale_key);
        title_filter.clear();
        char_len.clear();
        keys.clear();
        clear_overlay();
        scale_running = false;
    };

  protected:
    /*
     * Text overlay with the current filter
     */
    wf::cairo_text_t filter_overlay;
    float output_scale = 1.0f;
    /* render function */
    wf::effect_hook_t render_hook = [=] () { render(); };
    /* flag to indicate if render_hook is active */
    bool render_active = false;
    wf::option_wrapper_t<wf::color_t> bg_color{"scale-title-filter/bg_color"};
    wf::option_wrapper_t<wf::color_t> text_color{"scale-title-filter/text_color"};
    wf::option_wrapper_t<bool> show_overlay{"scale-title-filter/overlay"};
    wf::option_wrapper_t<int> font_size{"scale-title-filter/font_size"};

    void update_overlay()
    {
        if (!show_overlay || title_filter.empty())
        {
            /* remove any overlay */
            clear_overlay();
            return;
        }

        auto dim = output->get_screen_size();
        filter_overlay.render_text(
            title_filter,
            wf::cairo_text_t::params(font_size, bg_color, text_color, output_scale,
                dim));

        if (!render_active)
        {
            output->render->add_effect(&render_hook, wf::OUTPUT_EFFECT_OVERLAY);
            render_active = true;
        }

        int surface_width  = filter_overlay.tex.width;
        int surface_height = filter_overlay.tex.height;

        output->render->damage({
            dim.width / 2 - (int)(surface_width / output_scale / 2),
            dim.height / 2 - (int)(surface_height / output_scale / 2),
            (int)(surface_width / output_scale),
            (int)(surface_height / output_scale)
        });
    }

    /* render the current content of the overlay texture */
    void render()
    {
        auto out_fb = output->render->get_target_framebuffer();
        auto dim    = output->get_screen_size();
        if (output_scale != out_fb.scale)
        {
            output_scale = out_fb.scale;
            update_overlay();
        }

        const wf::simple_texture_t& tex = filter_overlay.tex;
        if (tex.tex == (GLuint) - 1)
        {
            return;
        }

        wf::geometry_t geometry{
            dim.width / 2 - (int)(tex.width / output_scale / 2),
            dim.height / 2 - (int)(tex.height / output_scale / 2),
            (int)(tex.width / output_scale),
            (int)(tex.height / output_scale)
        };
        auto damage = output->render->get_scheduled_damage() & geometry;
        auto ortho  = out_fb.get_orthographic_projection();

        OpenGL::render_begin(out_fb);
        for (auto& box : damage)
        {
            out_fb.logic_scissor(wlr_box_from_pixman_box(box));
            OpenGL::render_transformed_texture(tex.tex, geometry, ortho,
                glm::vec4(1.f), OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
        }

        OpenGL::render_end();
    }

    /* clear everything rendered by this plugin and deactivate rendering */
    void clear_overlay()
    {
        if (render_active)
        {
            output->render->rem_effect(&render_hook);
            auto dim = output->get_screen_size();
            int surface_width  = filter_overlay.tex.width;
            int surface_height = filter_overlay.tex.height;

            output->render->damage({
                dim.width / 2 - (int)(surface_width / output_scale / 2),
                dim.height / 2 - (int)(surface_height / output_scale / 2),
                (int)(surface_width / output_scale),
                (int)(surface_height / output_scale)
            });
            render_active = false;
        }
    }
};

DECLARE_WAYFIRE_PLUGIN(scale_title_filter);
