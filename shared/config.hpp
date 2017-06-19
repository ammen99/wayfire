#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <vector>
#include <string>
#include <unordered_map>

struct wayfire_key {
    uint32_t mod;
    uint32_t keyval;
};

struct wayfire_button {
    uint32_t mod;
    uint32_t button;
};

struct wayfire_color {
    float r, g, b, a;
};

struct wayfire_config_section {
    std::string name;
    int refresh_rate;
    std::unordered_map<std::string, std::string> options;

    std::string get_string(std::string name, std::string default_value);
    int get_int(std::string name, int default_value);
    /* reads the specified option which is interpreted as duration in milliseconds
     * returns the number of frames this duration is equivalent to */
    int get_duration(std::string name, int default_value);
    double get_double(std::string name, double default_value);

    wayfire_key get_key(std::string name,
            wayfire_key default_value);
    wayfire_button get_button(std::string name,
            wayfire_button default_value);
    wayfire_color get_color(std::string name,
            wayfire_color default_value);
};

class wayfire_config {
    std::vector<wayfire_config_section*> sections;
    int refresh_rate;

    public:
    wayfire_config(std::string file, int refresh_rate = -1);
    wayfire_config_section* get_section(std::string name);
};

#endif /* end of include guard: CONFIG_HPP */
