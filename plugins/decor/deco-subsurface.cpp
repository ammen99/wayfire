#define GLM_FORCE_RADIANS
#include <glm/gtc/matrix_transform.hpp>

#include <linux/input-event-codes.h>

#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/compositor-surface.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/core.hpp>
#include <wayfire/decorator.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/signal-definitions.hpp>
#include "deco-subsurface.hpp"
#include "deco-layout.hpp"
#include "deco-theme.hpp"

#include <wayfire/plugins/common/cairo-util.hpp>

#include <cairo.h>

class simple_decoration_surface : public wf::surface_interface_t,
    public wf::compositor_surface_t, public wf::decorator_frame_t_t
{
    bool _mapped = true;
    int current_thickness;
    int current_titlebar;

    wayfire_view view;
    wf::signal_callback_t title_set = [=] (wf::signal_data_t *data)
    {
        if (get_signaled_view(data) == view)
        {
            view->damage(); // trigger re-render
        }
    };

    void update_title(int width, int height, double scale)
    {
        int target_width  = width * scale;
        int target_height = height * scale;

        if ((title_texture.tex.width != target_width) ||
            (title_texture.tex.height != target_height) ||
            (title_texture.current_text != view->get_title()))
        {
            auto surface = theme.render_text(view->get_title(),
                target_width, target_height);
            cairo_surface_upload_to_texture(surface, title_texture.tex);
            cairo_surface_destroy(surface);
            title_texture.current_text = view->get_title();
        }
    }

    int width = 100, height = 100;

    bool active = true; // when views are mapped, they are usually activated

    struct
    {
        wf::simple_texture_t tex;
        std::string current_text = "";
    } title_texture;

    wf::decor::decoration_theme_t theme;
    wf::decor::decoration_layout_t layout;
    wf::region_t cached_region;

  public:
    simple_decoration_surface(wayfire_view view) :
        theme{},
        layout{theme, [=] (wlr_box box) {this->damage_surface_box(box); }}
    {
        this->view = view;
        view->connect_signal("title-changed", &title_set);
        view->connect_signal("subsurface-removed", &on_subsurface_removed);

        // make sure to hide frame if the view is fullscreen
        update_decoration_size();
    }

    virtual ~simple_decoration_surface()
    {
        view->disconnect_signal("title-changed", &title_set);
    }

    /* wf::surface_interface_t implementation */
    virtual bool is_mapped() const final
    {
        return _mapped;
    }

    wf::point_t get_offset() final
    {
        return {-current_thickness, -current_titlebar};
    }

    virtual wf::dimensions_t get_size() const final
    {
        return {width, height};
    }

    void render_title(const wf::framebuffer_t& fb,
        wf::geometry_t geometry)
    {
        update_title(geometry.width, geometry.height, fb.scale);
        OpenGL::render_texture(title_texture.tex.tex, fb, geometry,
            glm::vec4(1.0f), OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
    }

    void render_scissor_box(const wf::framebuffer_t& fb, wf::point_t origin,
        const wlr_box& scissor)
    {
        /* Clear background */
        wlr_box geometry{origin.x, origin.y, width, height};
        theme.render_background(fb, geometry, scissor, active);

        /* Draw title & buttons */
        auto renderables = layout.get_renderable_areas();
        for (auto item : renderables)
        {
            if (item->get_type() == wf::decor::DECORATION_AREA_TITLE)
            {
                OpenGL::render_begin(fb);
                fb.logic_scissor(scissor);
                render_title(fb, item->get_geometry() + origin);
                OpenGL::render_end();
            } else // button
            {
                item->as_button().render(fb,
                    item->get_geometry() + origin, scissor);
            }
        }
    }

    virtual void simple_render(const wf::framebuffer_t& fb, int x, int y,
        const wf::region_t& damage) override
    {
        wf::region_t frame = this->cached_region + wf::point_t{x, y};
        frame &= damage;

        for (const auto& box : frame)
        {
            render_scissor_box(fb, {x, y}, wlr_box_from_pixman_box(box));
        }
    }

    bool accepts_input(int32_t sx, int32_t sy) override
    {
        return pixman_region32_contains_point(cached_region.to_pixman(),
            sx, sy, NULL);
    }

    /* wf::compositor_surface_t implementation */
    virtual void on_pointer_enter(int x, int y) override
    {
        layout.handle_motion(x, y);
    }

    virtual void on_pointer_leave() override
    {
        layout.handle_focus_lost();
    }

    virtual void on_pointer_motion(int x, int y) override
    {
        handle_action(layout.handle_motion(x, y));
    }

    virtual void on_pointer_button(uint32_t button, uint32_t state) override
    {
        if (button != BTN_LEFT)
        {
            return;
        }

        handle_action(layout.handle_press_event(state == WLR_BUTTON_PRESSED));
    }

    void handle_action(wf::decor::decoration_layout_t::action_response_t action)
    {
        switch (action.action)
        {
          case wf::decor::DECORATION_ACTION_MOVE:
            return view->move_request();

          case wf::decor::DECORATION_ACTION_RESIZE:
            return view->resize_request(action.edges);

          case wf::decor::DECORATION_ACTION_CLOSE:
            return view->close();

          case wf::decor::DECORATION_ACTION_TOGGLE_MAXIMIZE:
            if (view->tiled_edges)
            {
                view->tile_request(0);
            } else
            {
                view->tile_request(wf::TILED_EDGES_ALL);
            }

            break;

          case wf::decor::DECORATION_ACTION_MINIMIZE:
            view->minimize_request(true);
            break;

          default:
            break;
        }
    }

    virtual void on_touch_down(int x, int y) override
    {
        layout.handle_motion(x, y);
        handle_action(layout.handle_press_event());
    }

    virtual void on_touch_motion(int x, int y) override
    {
        handle_action(layout.handle_motion(x, y));
    }

    virtual void on_touch_up() override
    {
        handle_action(layout.handle_press_event(false));
        layout.handle_focus_lost();
    }

    /* frame implementation */
    virtual wf::geometry_t expand_wm_geometry(
        wf::geometry_t contained_wm_geometry) override
    {
        contained_wm_geometry.x     -= current_thickness;
        contained_wm_geometry.y     -= current_titlebar;
        contained_wm_geometry.width += 2 * current_thickness;
        contained_wm_geometry.height += current_thickness + current_titlebar;

        return contained_wm_geometry;
    }

    virtual void calculate_resize_size(
        int& target_width, int& target_height) override
    {
        target_width  -= 2 * current_thickness;
        target_height -= current_thickness + current_titlebar;

        target_width  = std::max(target_width, 1);
        target_height = std::max(target_height, 1);
    }

    wf::signal_connection_t on_subsurface_removed = [&] (auto data)
    {
        auto ev = static_cast<wf::subsurface_removed_signal*>(data);
        if (ev->subsurface.get() == this)
        {
            unmap();
        }
    };

    void unmap()
    {
        _mapped = false;
        wf::emit_map_state_change(this);
    }

    virtual void notify_view_activated(bool active) override
    {
        if (this->active != active)
        {
            view->damage();
        }

        this->active = active;
    }

    virtual void notify_view_resized(wf::geometry_t view_geometry) override
    {
        view->damage();
        width  = view_geometry.width;
        height = view_geometry.height;

        layout.resize(width, height);
        if (!view->fullscreen)
        {
            this->cached_region = layout.calculate_region();
        }

        view->damage();
    }

    virtual void notify_view_tiled() override
    {}

    void update_decoration_size()
    {
        if (view->fullscreen)
        {
            current_thickness = 0;
            current_titlebar  = 0;
            this->cached_region.clear();
        } else
        {
            current_thickness = theme.get_border_size();
            current_titlebar  =
                theme.get_title_height() + theme.get_border_size();
            this->cached_region = layout.calculate_region();
        }
    }

    virtual void notify_view_fullscreen() override
    {
        update_decoration_size();

        if (!view->fullscreen)
        {
            notify_view_resized(view->get_wm_geometry());
        }
    }
};

void init_view(wayfire_view view)
{
    auto surf = std::make_unique<simple_decoration_surface>(view);
    auto ptr  = surf.get();

    view->add_subsurface(std::move(surf), true);
    view->set_decoration(ptr);
    view->damage();
}

void deinit_view(wayfire_view view)
{
    auto decor = dynamic_cast<simple_decoration_surface*>(
        view->get_decoration().get());
    if (!decor)
    {
        return;
    }

    decor->unmap();
    view->set_decoration(nullptr);
}
