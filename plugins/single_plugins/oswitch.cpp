#include <output.hpp>
#include <core.hpp>

class OutputSwitcher : public Plugin {
    KeyBinding switch_output, switch_output_with_window;
    Key without, with;

    public:
        void initOwnership() {
            owner->name = "oswitch";
            owner->compatAll = true;
        }

        void updateConfiguration() {
            using namespace std::placeholders;

            without = *options["switch_output"]->data.key;
            with    = *options["switch_output_with_window"]->data.key;

            if (without.key) {
                switch_output.key = without.key;
                switch_output.mod = without.mod;
                switch_output.active = true;
                switch_output.action = std::bind(std::mem_fn(&OutputSwitcher::handle_key), this, _1);
                output->hook->add_key(&switch_output);
            }

            if (with.key) {
                switch_output_with_window.key = with.key;
                switch_output_with_window.mod = with.mod;
                switch_output_with_window.active = true;
                switch_output_with_window.action = std::bind(std::mem_fn(&OutputSwitcher::handle_key), this, _1);
                output->hook->add_key(&switch_output_with_window);
            }
        }

        void init() {
            options.insert(newKeyOption("switch_output", Key{0, 0}));
            options.insert(newKeyOption("switch_output_with_window", Key{0, 0}));
        }

        void handle_key(EventContext ctx) {
            auto key = ctx.xev.xkey;
            auto now  = core->get_active_output();
            auto next = core->get_next_output();
            auto view = output->get_active_view();

            core->focus_output(next);

            if (key.key == with.key && key.mod == with.mod) {
                wlc_view_set_output(view->get_id(), next->get_handle());
            }
        }
};

extern "C" {
    Plugin *newInstance() {
        return new OutputSwitcher();
    }
}
