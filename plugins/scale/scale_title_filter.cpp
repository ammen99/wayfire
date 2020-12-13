#include <cctype>
#include <string>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/plugins/common/scale-signal.hpp>

#include <wayfire/render-manager.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>
#include <wayfire/plugins/common/simple-texture.hpp>
#include <cairo.h>

struct scale_title_filter : public wf::plugin_interface_t
{
    wf::option_wrapper_t<bool> case_sensitive{"scale_title_filter/case_sensitive"};
    std::string title_filter;
    /* since title filter is utf-8, here we store the length of each
     * character when adding them so backspace will work properly */
    std::vector<int> char_len;
    bool scale_running = false;

    bool should_show_view(wayfire_view view)
    {
        if (title_filter.empty())
        {
            return true;
        }

        if (!case_sensitive)
        {
            std::string title = view->get_title();
            std::string filter = title_filter;
            auto transform = [] (unsigned char c)
            {
                if (std::isspace(c))
                {
                    return ' ';
                }

                return (char)std::tolower(c);
            };
            std::transform(title.begin(), title.end(), title.begin(), transform);
            std::transform(filter.begin(), filter.end(), filter.begin(), transform);
            return title.find(filter) != std::string::npos;
        }

        return view->get_title().find(title_filter) != std::string::npos;
    }

  public:
    void init() override
    {
        grab_interface->name = "scale_title_filter";
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

            auto v   = static_cast<scale_filter_signal*>(data);
            v->hide |= !should_show_view(v->view);
        }
    };

    wf::signal_connection_t scale_key{[this] (wf::signal_data_t *data)
        {
            auto k =
                static_cast<wf::input_event_signal<wlr_event_keyboard_key>*>(data);
            if ((k->event->state != WL_KEYBOARD_KEY_STATE_PRESSED) ||
                (k->event->keycode == KEY_ESC) || (k->event->keycode == KEY_ENTER))
            {
                return;
            }

            bool changed = false;
            if (k->event->keycode == KEY_BACKSPACE)
            {
                if (!title_filter.empty())
                {
                    int len = char_len.back();
                    char_len.pop_back();
                    for (int i = 0; i < len; i++)
                    {
                        title_filter.pop_back();
                    }

                    changed = true;
                }
            } else
            {
                std::string tmp = wf::get_core().convert_keycode(
                    k->event->keycode + 8);
                if (!tmp.empty())
                {
                    char_len.push_back(tmp.length());
                    title_filter += tmp;
                    changed = true;
                }
            }

            if (changed)
            {
                LOGI("Title filter changed: ", title_filter);
                scale_activate_signal data;
                output->emit_signal("scale-activate", &data);
                update_overlay();
            }
        }
    };

    wf::signal_connection_t scale_end{[this] (wf::signal_data_t *data)
        {
            wf::get_core().disconnect_signal(&scale_key);
            title_filter.clear();
            char_len.clear();
            clear_overlay();
            scale_running = false;
        }
    };

  protected:
    /*
     * Text overlay with the current filter
     */
    wf::simple_texture_t tex;
    /* cairo context and surface for the text */
    cairo_t *cr = nullptr;
    cairo_surface_t *surface = nullptr;
    /* current width and height of the above surface */
    unsigned int surface_width  = 400;
    unsigned int surface_height = 300;
    /* render function */
    wf::effect_hook_t render_hook = [=] () { render(); };
    /* flag to indicate if render_hook is active */
    bool render_active = false;
    wf::option_wrapper_t<wf::color_t> bg_color{"scale_title_filter/bg_color"};
    wf::option_wrapper_t<wf::color_t> text_color{"scale_title_filter/text_color"};
    wf::option_wrapper_t<bool> show_overlay{"scale_title_filter/overlay"};
    wf::option_wrapper_t<int> font_size{"scale_title_filter/font_size"};

    void update_overlay()
    {
        if (!show_overlay || title_filter.empty())
        {
            /* remove any overlay */
            clear_overlay();
            return;
        }

        /* update overlay with the current text */
        if (!cr)
        {
            /* create with default size */
            surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surface_width,
                surface_height);
            cr = cairo_create(surface);
        }

        cairo_text_extents_t extents;
        cairo_font_extents_t font_extents;
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
            CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, font_size);
        cairo_text_extents(cr, title_filter.c_str(), &extents);
        cairo_font_extents(cr, &font_extents);

        double xpad    = 10.0;
        double ypad    = 0.2 * (font_extents.ascent + font_extents.descent);
        unsigned int w = (unsigned int)(extents.width + 2 * xpad);
        unsigned int h =
            (unsigned int)(font_extents.ascent + font_extents.descent + 2 * ypad);
        auto dim = output->get_screen_size();
        if ((int)w > dim.width)
        {
            w = dim.width;
        }

        if ((int)h > dim.height)
        {
            h = dim.height;
        }

        if ((w > surface_width) || (h > surface_height))
        {
            cairo_free();
            surface_width  = w;
            surface_height = h;
            surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surface_width,
                surface_height);
            cr = cairo_create(surface);
        }

        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);

        unsigned int x = (surface_width - w) / 2;
        unsigned int y = (surface_height - h) / 2;
        unsigned int r = h > 20 ? 20 : (h - 2) / 2;
        const wf::color_t& bg   = bg_color;
        const wf::color_t& text = text_color;

        cairo_move_to(cr, x + r, y);
        cairo_line_to(cr, x + w - r, y);
        cairo_curve_to(cr, x + w, y, x + w, y, x + w, y + r);
        cairo_line_to(cr, x + w, y + h - r);
        cairo_curve_to(cr, x + w, y + h, x + w, y + h, x + w - r, y + h);
        cairo_line_to(cr, x + r, y + h);
        cairo_curve_to(cr, x, y + h, x, y + h, x, y + h - r);
        cairo_line_to(cr, x, y + r);
        cairo_curve_to(cr, x, y, x, y, x + r, y);

        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, bg.a);
        cairo_fill(cr);

        x += xpad;
        y += ypad + font_extents.ascent;
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
            CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, font_size);
        cairo_move_to(cr, x - extents.x_bearing, y);
        cairo_set_source_rgba(cr, text.r, text.g, text.b, text.a);
        cairo_show_text(cr, title_filter.c_str());

        cairo_surface_flush(surface);
        OpenGL::render_begin();
        cairo_surface_upload_to_texture(surface, tex);
        OpenGL::render_end();

        if (!render_active)
        {
            output->render->add_effect(&render_hook, wf::OUTPUT_EFFECT_OVERLAY);
            render_active = true;
        }

        output->render->damage_whole();
    }

    /* render the current content of the overlay texture */
    void render()
    {
        if (tex.tex == (GLuint) - 1)
        {
            return;
        }

        auto out_fb = output->render->get_target_framebuffer();
        auto dim    = output->get_screen_size();
        wf::geometry_t geometry{(dim.width - tex.width) / 2,
            (dim.height - tex.height) / 2, tex.width, tex.height};
        auto ortho = out_fb.get_orthographic_projection();

        OpenGL::render_begin(out_fb);
        GL_CALL(glEnable(GL_BLEND));
        OpenGL::render_transformed_texture(tex.tex, geometry, ortho,
            glm::vec4(1.f), OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
        GL_CALL(glDisable(GL_BLEND));
        OpenGL::render_end();
    }

    /* clear everything rendered by this plugin and deactivate rendering */
    void clear_overlay()
    {
        if (render_active)
        {
            output->render->rem_effect(&render_hook);
            output->render->damage_whole();
            render_active = false;
        }

        cairo_free();
    }

    void cairo_free()
    {
        if (cr)
        {
            cairo_destroy(cr);
        }

        if (surface)
        {
            cairo_surface_destroy(surface);
        }

        cr = nullptr;
        surface = nullptr;
    }
};

DECLARE_WAYFIRE_PLUGIN(scale_title_filter);
