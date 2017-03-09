#include <output.hpp>

#define NUMBER_COMMANDS 10

#define TYPE_COMMAND 1
#define TYPE_BINDING 2

namespace {
    std::string numToString(int a) {
        std::string str = "";
        while(a > 0)
            str = char(a % 10 + '0') + str,
            a /= 10;

        if(str.length() < 2)
            str = '0' + str;

        return str;
    }

    std::string getStringFromCommandNumber(int no, int type) {
        std::string s = numToString(no) + "_";

        if(type == TYPE_COMMAND)
            s += "command";
        if(type == TYPE_BINDING)
            s += "binding";

        return s;
    }
}

class wayfire_command : public wayfire_plugin_t {
    std::unordered_map<std::string, key_callback> commands;

    public:
    void init(wayfire_config *config) {
        owner->name = "command";
        owner->compatAll = true;
        using namespace std::placeholders;

        for(int i = 1; i <= NUMBER_COMMANDS; i++) {
            auto str1 = getStringFromCommandNumber(i, TYPE_COMMAND);
            auto str2 = getStringFromCommandNumber(i, TYPE_BINDING);

            auto com = *options[str1]->data.sval;
            if(com == "")
                continue;

            auto key = *options[str2]->data.key;
            if(key.mod == 0 && key.key == 0)
                continue;

            commands[com].action = std::bind(std::mem_fn(&Commands::onCommandActivated), this, _1);
            commands[com].type   = BindingTypePress;
            commands[com].key    = key.key;
            commands[com].mod    = key.mod;
            output->hook->add_key(&commands[com], true);
        }

        for(int i = 1; i <= NUMBER_COMMANDS; i++) {
            auto str1 = getStringFromCommandNumber(i, TYPE_COMMAND);
            auto str2 = getStringFromCommandNumber(i, TYPE_BINDING);

            options.insert(newStringOption(str1, ""));
            options.insert(newKeyOption(str2, Key{0, 0}));
        }
    }

    void onCommandActivated(EventContext ctx){
        auto xev = ctx.xev.xkey;

        for(auto com : commands)
            if (output->input->check_key(&com.second, xev.key, xev.mod))
                core->run(com.first.c_str());
    }
};

extern "C" {
    Plugin *newInstance() {
        return new Commands();
    }
}
