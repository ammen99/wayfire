#include <plugin.hpp>
#include <output.hpp>
#include <view.hpp>
#include <cwctype>
#include <cstdio>
#include <signal-definitions.hpp>
#include <assert.h>
#include <map>

using std::string;

/*
rules syntax:

title (T) / title contains (T) / app-id (T) / app-id contains (T) (created/destroyed/maximized/fullscreened) ->
    move X Y | resize W H | (un)set fullscreen | (un)set maximized

where (T) is a text surrounded by parenthesis, for ex. (tilix)
contains (T) means that (T) can be found anywhere in the title/app-id string

X Y W H are simply integers indicating the position where the view
should be placed and W H are positive integers indicating size

examples:

title contains Chrome created -> set maximized
app-id tilix created -> move 0 0

 */

static string trim(string x)
{
    int i = 0, j = x.length() - 1;
    while(i < (int)x.length() && std::iswspace(x[i])) ++i;
    while(j >= 0 && std::iswspace(x[j])) --j;

    if (i <= j)
        return x.substr(i, j - i + 1);
    else
        return "";
}

static bool starts_with(string x, string y)
{
    return x.length() >= y.length() && x.substr(0, y.length()) == y;
}

static bool ends_with(string x, string y)
{
    return x.length() >= y.length() && x.substr(x.length() - y.length()) == y;
}


class wayfire_window_rules : public wf::plugin_interface_t
{
    using verification_func = std::function<bool(wayfire_view, std::string)>;

    struct verificator
    {
        verification_func func;
        std::string atom;
    };

    std::vector<verificator> verficators =
    {
        {  [] (wayfire_view view, std::string match) -> bool
            {
                auto title = view->get_title();
                return title.find(match) != std::string::npos;
            },
            "title contains"
        },
        { [] (wayfire_view view, std::string match) -> bool
            {
                auto title = view->get_title();
                return title == match;
            },
            "title"
        },
        {  [] (wayfire_view view, std::string match) -> bool
            {
                auto app_id = view->get_app_id();
                return app_id.find(match) != std::string::npos;
            },
            "app-id contains"
        },
        {  [] (wayfire_view view, std::string match) -> bool
            {
                auto app_id = view->get_app_id();
                return app_id == match;
            },
            "app-id"
        },
    };

    std::vector<std::string> events = {
        "created", "maximized", "fullscreened"
    };

    using action_func = std::function<void(wayfire_view view)>;

    struct lambda_executor
    {
        verification_func verify;
        std::string verification_string;
        action_func action;
    };

    using rule_func = std::function<void(wayfire_view)>;
    struct rule
    {
        std::string signal;
        std::function<void(wayfire_view)> func;
    };

    rule parse_add_rule(std::string rule)
    {
        std::string predicate, action;
        struct rule result;

        size_t pos = 0;
        for (; pos < rule.size() - 2; ++pos)
        {
            if (rule[pos] == '-' && rule[pos + 1] == '>')
                break;
        }

        /* first condition is so that there is no underflow in unsigned arithmetic */
        if (rule.size() <= 5 || pos >= rule.size() - 2 || pos < 1)
            return result;

        predicate = trim(rule.substr(0, pos));
        std::string event;
        action = trim(rule.substr(pos + 2, rule.size() - pos - 1));

        for (auto ev : events)
        {
            if (ends_with(predicate, ev))
            {
                event = ev;
                predicate = trim(predicate.substr(0, predicate.length() - ev.length()));
                break;
            }
        }

        lambda_executor exec;
        exec.verify = nullptr;
        exec.action = nullptr;

        for (const auto& pred : verficators)
        {
            if (starts_with(predicate, pred.atom))
            {
                exec.verify = pred.func;
                exec.verification_string =
                    trim(predicate.substr(pred.atom.length(),
                                          predicate.length() - pred.atom.length()));
                break;
            }
        }

        if (!exec.verify || !event.length())
            return result;

        if (starts_with(action, "move"))
        {
            int x, y;
            int t = std::sscanf(action.c_str(), "move %d %d", &x, &y);

            if (t != 2)
                return result;

            exec.action = [x,y] (wayfire_view view) {
                auto og = view->get_output()->get_relative_geometry();
                view->move(og.x + x, og.y + y);
            };
        } else if (starts_with(action, "resize"))
        {
            int w, h;
            int t = std::sscanf(action.c_str(), "resize %d %d", &w, &h);

            if (t != 2 || w <= 0 || h <= 0)
                return result;

            exec.action = [w,h] (wayfire_view view) mutable {
                GetTuple(sw, sh, view->get_output()->get_screen_size());
                if (w > 100000)
                    w = sw;
                if (h > 100000)
                    h = sh;
                view->resize(w, h);
            };
        } else if (ends_with(action, "set maximized"))
        {
            exec.action = [action] (wayfire_view view)
            {
                view_maximized_signal data;
                data.view = view;
                data.state = starts_with(action, "set");
                view->get_output()->emit_signal("view-maximized-request", &data);
            };
        }

        else if (ends_with(action, "set fullscreen"))
        {
            exec.action = [action] (wayfire_view view)
            {
                view_fullscreen_signal data;
                data.view = view;
                data.state = starts_with(action, "set");
                view->get_output()->emit_signal("view-fullscreen-request", &data);
            };
        }


        if (!exec.action)
            return result;

        result.signal = event;
        result.func = [exec] (wayfire_view view)
        {
            if (exec.verify(view, exec.verification_string))
                exec.action(view);
        };

        return result;
    }

    wf::signal_callback_t created, maximized, fullscreened;

    std::map<std::string, std::vector<rule_func>> rules_list;

    public:
    void init(wayfire_config *config)
    {
        auto section = config->get_section("window-rules");
        for (auto opt : section->options)
        {
            auto rule = parse_add_rule(opt->as_string());
            rules_list[rule.signal].push_back(rule.func);
        }

        created = [=] (wf::signal_data_t *data)
        {
            for (const auto& rule : rules_list["created"])
                rule(get_signaled_view(data));
        };
        output->connect_signal("map-view", &created);

        maximized = [=] (wf::signal_data_t *data)
        {
            auto conv = static_cast<view_maximized_signal*> (data);
            assert(conv);

            if (!conv->state)
                return;

            for (const auto& rule : rules_list["maximized"])
                rule(conv->view);
        };
        output->connect_signal("view-maximized", &maximized);

        fullscreened = [=] (wf::signal_data_t *data)
        {
            auto conv = static_cast<view_fullscreen_signal*> (data);
            assert(conv);

            if (!conv->state)
                return;

            for (const auto& rule : rules_list["fullscreened"])
                rule(conv->view);
        };
        output->connect_signal("view-fullscreen", &fullscreened);
    }

    void fini()
    {
        output->disconnect_signal("map-view", &created);
        output->disconnect_signal("view-maximized", &maximized);
        output->disconnect_signal("view-fullscreen", &fullscreened);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_window_rules);
