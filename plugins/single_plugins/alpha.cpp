/*
 * The MIT License (MIT)
 * 
 * Copyright (c) 2018 Scott Moreau
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <core.hpp>
#include <view.hpp>
#include <plugin.hpp>
#include <output.hpp>
#include "view-transform.hpp"
#include "workspace-manager.hpp"

class wayfire_alpha : public wf::plugin_interface_t
{
    wf_option modifier, min_value;

    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "alpha";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_DESKTOP;

        auto section = config->get_section("alpha");
        modifier = section->get_option("modifier", "<alt>");
        min_value = section->get_option("min_value", "0.1");

        min_value->add_updated_handler(&min_value_changed);
        output->add_axis(modifier, &axis_cb);
    }

    void update_alpha(wayfire_view view, float delta)
    {
        wf_2D_view *transformer;
        float alpha;

        if (!view->get_transformer("alpha"))
            view->add_transformer(std::make_unique<wf_2D_view> (view), "alpha");

        transformer = dynamic_cast<wf_2D_view*> (view->get_transformer("alpha").get());
        alpha = transformer->alpha;

        alpha -= delta * 0.003;

        if (alpha > 1.0)
            alpha = 1.0;

        if (alpha == 1.0)
            return view->pop_transformer("alpha");

        if (alpha < min_value->as_double())
            alpha = min_value->as_double();

        if (transformer->alpha != alpha)
        {
            transformer->alpha = alpha;
            view->damage();
        }
    }

    axis_callback axis_cb = [=] (wlr_event_pointer_axis* ev)
    {
        if (!output->activate_plugin(grab_interface))
            return;

        output->deactivate_plugin(grab_interface);

        auto view = wf::get_core().get_cursor_focus_view();
        if (!view)
            return;

        auto layer = output->workspace->get_view_layer(view);

        if (layer == wf::LAYER_BACKGROUND)
            return;

        if (ev->orientation == WLR_AXIS_ORIENTATION_VERTICAL)
            update_alpha(view, ev->delta);
    };

    wf_option_callback min_value_changed = [=] ()
    {
        for (auto& view : output->workspace->get_views_in_layer(wf::ALL_LAYERS))
        {
            if (!view->get_transformer("alpha"))
                continue;

            wf_2D_view *transformer = dynamic_cast<wf_2D_view*> (view->get_transformer("alpha").get());

            if (transformer->alpha < min_value->as_double())
            {
                transformer->alpha = min_value->as_double();
                view->damage();
            }

        }
    };

    void fini()
    {
        for (auto& view : output->workspace->get_views_in_layer(wf::ALL_LAYERS))
        {
            if (view->get_transformer("alpha"))
                view->pop_transformer("alpha");
        }

        min_value->rem_updated_handler(&min_value_changed);
        output->rem_binding(&axis_cb);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_alpha);
