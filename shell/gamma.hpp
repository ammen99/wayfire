#ifndef GAMMA_HPP
#define GAMMA_HPP

#include <cstdint>
#include <wayland-client.h>

class wayfire_config;

class gamma_adjust {
    int daytime_temp;
    int nighttime_temp;

    int day_start, day_end;

    uint32_t output, gamma_size;

    wl_array gamma_value[3];

    int current_temp = 6500;
    void set_gamma(int temp);
    void continuous_change(int from, int to);

    void adjustment_loop();

    public:
    gamma_adjust(uint32_t _output, uint32_t _gamma_size, wayfire_config *config);
};


#endif /* end of include guard: GAMMA_HPP */
