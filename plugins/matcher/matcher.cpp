#include "matcher.hpp"
#include "matcher-ast.hpp"

#include <debug.hpp>
#include <singleton-plugin.hpp>
#include <core.hpp>
#include <output.hpp>
#include <workspace-manager.hpp>

namespace wf
{
    namespace matcher
    {
        std::string get_view_type(wayfire_view view)
        {
            if (view->role == VIEW_ROLE_TOPLEVEL)
                return "toplevel";
            if (view->role == VIEW_ROLE_UNMANAGED)
                return "x-or";

            if (!view->get_output())
                return "unknown";

            uint32_t layer = view->get_output()->workspace->get_view_layer(view);
            if (layer == LAYER_BACKGROUND || layer == LAYER_BOTTOM)
                return "background";
            if (layer == LAYER_TOP)
                return "panel";
            if (layer == LAYER_LOCK)
                return "overlay";

            return "unknown";
        };

        class default_view_matcher : public view_matcher
        {
            std::unique_ptr<expression_t> expr;
            wf_option match_option;

            wf_option_callback on_match_string_updated = [=] ()
            {
                auto result = parse_expression(match_option->as_string());
                if (!result.first)
                {
                    LOGE("Failed to load match expression %s:\n%s",
                        match_option->as_string().c_str(), result.second.c_str());
                }

                this->expr = std::move(result.first);
            };

            public:
            default_view_matcher(wf_option option)
                : match_option(option)
            {
                on_match_string_updated();
                match_option->add_updated_handler(&on_match_string_updated);
            }

            virtual ~default_view_matcher()
            {
                match_option->rem_updated_handler(&on_match_string_updated);
            }

            virtual bool matches(wayfire_view view) const
            {
                if (!expr || !view->is_mapped())
                    return false;

                view_t data;
                data.title = view->get_title();
                data.app_id = view->get_app_id();
                data.type = get_view_type(view);
                data.focuseable = view->is_focuseable() ?  "true" : "false";

                return expr->evaluate(data);
            }
        };

        class matcher_plugin
        {
            signal_callback_t on_new_matcher_request = [=] (signal_data_t *data)
            {
                auto ev = static_cast<match_signal*> (data);
                ev->result = std::make_unique<default_view_matcher> (ev->expression);
            };

            signal_callback_t on_matcher_evaluate = [=] (signal_data_t *data)
            {
                auto ev = static_cast<match_evaluate_signal*> (data);
                auto expr =
                    dynamic_cast<default_view_matcher*> (ev->matcher.get());

                if (expr)
                    ev->result = expr->matches(ev->view);
            };

            public:
            matcher_plugin()
            {
                wf::get_core().connect_signal(WF_MATCHER_CREATE_QUERY_SIGNAL,
                    &on_new_matcher_request);
                wf::get_core().connect_signal(WF_MATCHER_EVALUATE_SIGNAL,
                    &on_matcher_evaluate);
            }
        };

        class matcher_singleton : public wf::singleton_plugin_t<matcher_plugin>
        {
            bool is_unloadable() override {return false;}
        };
    }
}

DECLARE_WAYFIRE_PLUGIN(wf::matcher::matcher_singleton);
