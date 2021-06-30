#include <map>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/scale-signal.hpp>
#include <wayfire/plugins/scale-transform.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/util/log.hpp>
#include <linux/input-event-codes.h>

class scale_decor_overlay_t;

class scale_decor_t : public wf::plugin_interface_t
{
  public:
    std::map<wayfire_view, scale_decor_overlay_t*> overlays;
    /* TODO: make these possible to change in config */
    const wf::dimensions_t size = {40, 30};
    const wf::dimensions_t pad  = {5, 5};
    const wf::color_t overlay_color = {0.8, 0.0, 0.0, 1.0};
    const wf::color_t active_color  = {0.0, 0.0, 0.8, 1.0};
    wayfire_view selected_view = nullptr;

    wf::signal_connection_t view_filter = [this] (auto)
    {
        mouse_update.disconnect();
        wf::get_core().connect_signal("pointer_button", &mouse_update);
    };
    wf::signal_connection_t scale_end = [this] (auto)
    {
        mouse_update.disconnect();
    };
    wf::signal_connection_t add_overlay;
    wf::signal_connection_t mouse_update;

    bool unselect_view();

    void init() override
    {
        grab_interface->name = "scale-decor";
        grab_interface->capabilities = 0;

        output->connect_signal("scale-filter", &view_filter);
        output->connect_signal("scale-transformer-added", &add_overlay);
        output->connect_signal("scale-end", &scale_end);
    }

    void fini() override;

    scale_decor_t();
};

class scale_decor_overlay_t : public wf::scale_transformer_t::overlay_t
{
  public:
    wf::geometry_t geom;
    bool selected = false;
    wf::scale_transformer_t& tr;
    scale_decor_t& parent;
    wayfire_view view;

    bool pre_render()
    {
        auto box = view->get_wm_geometry();
        box = tr.trasform_box_without_padding(box);
        auto size = parent.size;
        auto pad  = parent.pad;
        geom = {box.x + box.width - size.width - pad.width,
            box.y - size.height - pad.height,
            size.width, size.height};
        return false;
    }

    void render(const wf::framebuffer_t& fb, const wf::region_t& damage)
    {
        auto our_damage = damage & geom;
        auto ortho = fb.get_orthographic_projection();
        OpenGL::render_begin(fb);
        for (const auto& box : our_damage)
        {
            fb.logic_scissor(wlr_box_from_pixman_box(box));
            OpenGL::render_rectangle(geom,
                selected ? parent.active_color : parent.overlay_color,
                ortho);
        }

        OpenGL::render_end();
    }

    scale_decor_overlay_t(wf::scale_transformer_t& tr_, scale_decor_t& parent_) :
        tr(tr_), parent(parent_), view(tr.get_transformed_view())
    {
        pre_render();
        pre_hook = [this] ()
        {
            return pre_render();
        };
        render_hook = [this] (
            const wf::framebuffer_t& fb,
            const wf::region_t& damage)
        {
            render(fb, damage);
        };
        view_padding.top  = parent.size.height + parent.pad.height;
        scale_padding.top = parent.size.height + parent.pad.height;
    }

    ~scale_decor_overlay_t()
    {
        parent.overlays.erase(view);
    }
};

void scale_decor_t::fini()
{
    view_filter.disconnect();
    add_overlay.disconnect();
    scale_end.disconnect();
    mouse_update.disconnect();

    for (auto it = overlays.begin(); it != overlays.end();)
    {
        auto ol = it->second;
        it = overlays.erase(it);
        ol->tr.rem_overlay(ol);
    }
}

bool scale_decor_t::unselect_view()
{
    bool ret = false;
    if (selected_view)
    {
        auto it = overlays.find(selected_view);
        if (it != overlays.end())
        {
            ret = true;
            it->second->selected = false;
            output->render->damage(it->second->geom);
        }

        selected_view = nullptr;
    }

    return ret;
}

scale_decor_t::scale_decor_t() :
    add_overlay{[this] (wf::signal_data_t *data)
    {
        auto signal = static_cast<scale_transformer_added_signal*>(data);
        auto tr     = signal->transformer;
        auto ol     = new scale_decor_overlay_t(*tr, *this);

        tr->add_overlay(std::unique_ptr<wf::scale_transformer_t::overlay_t>(ol), 2);

        overlays.insert({tr->get_transformed_view(), ol});
    }},
    mouse_update{[this] (wf::signal_data_t *data)
    {
        auto signal =
            static_cast<wf::input_event_signal<wlr_event_pointer_button>*>(data);
        if (signal->event->button != BTN_LEFT)
        {
            return;
        }

        if (signal->event->state == WLR_BUTTON_PRESSED)
        {
            unselect_view();
            auto coords = wf::get_core().get_cursor_position();
            for (auto& ol : overlays)
            {
                if (ol.second->geom & coords)
                {
                    selected_view = ol.first;
                    ol.second->selected = true;
                    output->render->damage(ol.second->geom);
                    break;
                }
            }
        } else if (signal->event->state == WLR_BUTTON_RELEASED)
        {
            auto view = selected_view;
            if (unselect_view())
            {
                view->close();
            }
        }
    }
}
{}

DECLARE_WAYFIRE_PLUGIN(scale_decor_t);
