#include "hotspot-manager.hpp"

void wf::hotspot_instance_t::process_input_motion(wf::point_t gc)
{
    if (!(hotspot_geometry[0] & gc) && !(hotspot_geometry[1] & gc))
    {
        timer.disconnect();
        this->armed = true;
        return;
    }

    if (!timer.is_connected() && this->armed)
    {
        this->armed = false;
        timer.set_timeout(timeout_ms, [=] ()
        {
            callback(this->edges);
            return false;
        });
    }
}

wf::geometry_t wf::hotspot_instance_t::pin(wf::dimensions_t dim) noexcept
{
    auto og = output->get_layout_geometry();

    wf::geometry_t result;
    result.width  = dim.width;
    result.height = dim.height;

    if (this->edges & OUTPUT_EDGE_LEFT)
    {
        result.x = og.x;
    } else if (this->edges & OUTPUT_EDGE_RIGHT)
    {
        result.x = og.x + og.width - dim.width;
    } else
    {
        result.x = og.x + og.width / 2 - dim.width / 2;
    }

    if (this->edges & OUTPUT_EDGE_TOP)
    {
        result.y = og.y;
    } else if (this->edges & OUTPUT_EDGE_BOTTOM)
    {
        result.y = og.y + og.height - dim.height;
    } else
    {
        result.y = og.y + og.height / 2 - dim.height / 2;
    }

    // Need to clamp if the region is very wide
    return wf::clamp(result, og);
}

void wf::hotspot_instance_t::recalc_geometry() noexcept
{
    uint32_t cnt_edges = __builtin_popcount(edges);

    if (cnt_edges == 2)
    {
        hotspot_geometry[0] = pin({away, along});
        hotspot_geometry[1] = pin({along, away});
    } else
    {
        wf::dimensions_t dim;
        if (edges & (OUTPUT_EDGE_LEFT | OUTPUT_EDGE_RIGHT))
        {
            dim = {away, along};
        } else
        {
            dim = {along, away};
        }

        hotspot_geometry[0] = pin(dim);
        hotspot_geometry[1] = hotspot_geometry[0];
    }
}

wf::hotspot_instance_t::hotspot_instance_t(wf::output_t *output, uint32_t edges,
    uint32_t along, uint32_t away, int32_t timeout,
    std::function<void(uint32_t)> callback)
{
    output->connect_signal("configuration-changed", &on_output_config_changed);
    wf::get_core().connect_signal("pointer_motion", &on_motion_event);
    wf::get_core().connect_signal("tablet_axis", &on_motion_event);
    wf::get_core().connect_signal("touch_motion", &on_touch_motion_event);

    this->edges = edges;
    this->along = along;
    this->away  = away;
    this->timeout_ms = timeout;
    this->output     = output;
    this->callback   = callback;

    recalc_geometry();

    // callbacks
    on_motion_event.set_callback([=] (wf::signal_data_t *data)
    {
        auto gcf = wf::get_core().get_cursor_position();
        wf::point_t gc{(int)gcf.x, (int)gcf.y};
        process_input_motion(gc);
    });

    on_touch_motion_event.set_callback([=] (wf::signal_data_t *data)
    {
        auto gcf = wf::get_core().get_touch_position(0);
        wf::point_t gc{(int)gcf.x, (int)gcf.y};
        process_input_motion(gc);
    });

    on_output_config_changed.set_callback([=] (wf::signal_data_t*)
    {
        recalc_geometry();
    });
}

void wf::hotspot_manager_t::update_hotspots(const container_t& activators)
{
    hotspots.clear();
    for (const auto& opt : activators)
    {
        auto opt_hotspots = opt->activated_by->get_value().get_hotspots();
        for (const auto& hs : opt_hotspots)
        {
            auto activator_cb = opt->callback;
            auto callback = [activator_cb] (uint32_t edges)
            {
                wf::activator_data_t data = {
                    .source = activator_source_t::HOTSPOT,
                    .activation_data = edges,
                };
                (*activator_cb)(data);
            };

            auto instance = std::make_unique<hotspot_instance_t>(output,
                hs.get_edges(), hs.get_size_along_edge(),
                hs.get_size_away_from_edge(), hs.get_timeout(), callback);
            hotspots.push_back(std::move(instance));
        }
    }
}
