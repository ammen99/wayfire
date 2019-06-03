#ifndef WF_MATCHER_PLUGIN_HPP
#define WF_MATCHER_PLUGIN_HPP

#include <core.hpp>
#include <view.hpp>
#include <config.hpp>

namespace wf
{
    namespace matcher
    {
        class view_matcher
        {
            public:
            virtual ~view_matcher() = default;
        };

        struct match_signal : public signal_data_t
        {
            std::unique_ptr<view_matcher> result;
            wf_option expression;
        };

#define WF_MATCHER_CREATE_QUERY_SIGNAL "matcher-create-query"

        /* Tries to create a view matcher on the given domain (usually the output
         * of the plugin) with the given expression. May return null */
        std::unique_ptr<view_matcher> get_matcher(wf_option expression)
        {
            match_signal data;
            data.expression = expression;
            get_core().emit_signal(WF_MATCHER_CREATE_QUERY_SIGNAL, &data);
            return std::move(data.result);
        }

        struct match_evaluate_signal : public signal_data_t
        {
            nonstd::observer_ptr<view_matcher> matcher;
            wayfire_view view;
            bool result;
        };

#define WF_MATCHER_EVALUATE_SIGNAL "matcher-evaluate-match"
        bool evaluate(const std::unique_ptr<view_matcher>& matcher,
            wayfire_view view)
        {
            match_evaluate_signal data;
            data.matcher = nonstd::make_observer(matcher.get());
            data.view = view;
            data.result = false; // by default

            get_core().emit_signal(WF_MATCHER_EVALUATE_SIGNAL, &data);
            return data.result;
        }
    }
}

#endif /* end of include guard: WF_MATCHER_PLUGIN_HPP */
