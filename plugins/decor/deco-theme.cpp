#include "deco-theme.hpp"
#include <wayfire/core.hpp>
#include <wayfire/opengl.hpp>
#include <config.h>
#include <map>

namespace wf
{
namespace decor
{
/** Create a new theme with the default parameters */
decoration_theme_t::decoration_theme_t()
{}

/** @return The available height for displaying the title */
int decoration_theme_t::get_title_height() const
{
    return title_height;
}

/** @return The available border for resizing */
int decoration_theme_t::get_border_size() const
{
    return border_size;
}

/**
 * Fill the given rectange with the background color(s).
 *
 * @param fb The target framebuffer, must have been bound already
 * @param rectangle The rectangle to redraw.
 * @param scissor The GL scissor rectangle to use.
 * @param active Whether to use active or inactive colors
 */
void decoration_theme_t::render_background(const wf::framebuffer_t& fb,
    wf::geometry_t rectangle, const wf::geometry_t& scissor, bool active) const
{
    wf::color_t color = active ? active_color : inactive_color;
    OpenGL::render_begin(fb);
    fb.logic_scissor(scissor);
    OpenGL::render_rectangle(rectangle, color, fb.get_orthographic_projection());
    OpenGL::render_end();
}

/**
 * Render the given text on a cairo_surface_t with the given size.
 * The caller is responsible for freeing the memory afterwards.
 */
cairo_surface_t*decoration_theme_t::render_text(std::string text,
    int width, int height) const
{
    const auto format = CAIRO_FORMAT_ARGB32;
    auto surface = cairo_image_surface_create(format, width, height);
    auto cr = cairo_create(surface);

    const float font_scale = 0.8;
    const float font_size  = height * font_scale;

    // render text
    cairo_select_font_face(cr, ((std::string)font).c_str(),
        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_source_rgba(cr, 1, 1, 1, 1);

    cairo_set_font_size(cr, font_size);
    cairo_move_to(cr, 0, font_size);

    cairo_text_extents_t ext;
    cairo_text_extents(cr, text.c_str(), &ext);
    cairo_show_text(cr, text.c_str());
    cairo_destroy(cr);

    return surface;
}

static struct icon_cache_t : public noncopyable_t
{
    ~icon_cache_t()
    {
        for (auto& icon : cached_icons)
        {
            cairo_surface_destroy(icon.second);
        }
    }

    std::map<wf::decor::button_type_t, cairo_surface_t*> cached_icons;
    cairo_surface_t *load_icon(wf::decor::button_type_t type)
    {
        if (cached_icons.count(type) == 0)
        {
            std::string resource_path =
                INSTALL_PREFIX "/share/wayfire/decoration/resources/";
            switch (type)
            {
              case BUTTON_CLOSE:
                resource_path += "close.png";
                break;

              case BUTTON_TOGGLE_MAXIMIZE:
                resource_path += "maximize.png";
                break;

              case BUTTON_MINIMIZE:
                resource_path += "minimize.png";
                break;

              default:
                assert(false);
            }

            cached_icons[type] =
                cairo_image_surface_create_from_png(resource_path.c_str());
        }

        return cached_icons[type];
    }
} cache;

cairo_surface_t*decoration_theme_t::get_button_surface(button_type_t button,
    const button_state_t& state) const
{
    cairo_surface_t *button_icon    = cache.load_icon(button);
    cairo_surface_t *button_surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, state.width, state.height);
    auto cr = cairo_create(button_surface);

    /* Clear the button background */
    cairo_rectangle(cr, 0, 0, state.width, state.height);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_fill(cr);

    /* Render button itself */
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_rectangle(cr, 0, 0, state.width, state.height);

    /* Border */
    cairo_set_line_width(cr, state.border);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    cairo_stroke_preserve(cr);

    // log_info("theme render button with %d %.2f", state.pressed,
    // state.hover_progress);
    /* Background */
    wf::color_t base_background = {0.5, 0.5, 0.5, 0.7};
    wf::color_t hover_add_background = {0.2, 0.2, 0.2, 0.2};
    cairo_set_source_rgba(cr,
        base_background.r + hover_add_background.r * state.hover_progress,
        base_background.g + hover_add_background.g * state.hover_progress,
        base_background.b + hover_add_background.b * state.hover_progress,
        base_background.a + hover_add_background.a * state.hover_progress);
    cairo_fill_preserve(cr);

    /* Icon */
    cairo_scale(cr,
        1.0 * state.width / cairo_image_surface_get_width(button_icon),
        1.0 * state.height / cairo_image_surface_get_height(button_icon));
    cairo_set_source_surface(cr, button_icon, 0, 0);
    cairo_fill(cr);

    cairo_destroy(cr);

    return button_surface;
}
}
}
