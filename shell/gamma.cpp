#include "gamma.hpp"
#include "window.hpp"
#include "config.hpp"
#include <assert.h>
#include <functional>
#include <thread>

const double adjustment[][3] = {
    {1.00000000, 0.71976951, 0.42860152}, /* 3000K */
    {1.00000000, 0.73288760, 0.45366838}, {1.00000000, 0.74542112, 0.47793608},
    {1.00000000, 0.75740814, 0.50145662}, {1.00000000, 0.76888303, 0.52427322},
    {1.00000000, 0.77987699, 0.54642268}, {1.00000000, 0.79041843, 0.56793692},
    {1.00000000, 0.80053332, 0.58884417}, {1.00000000, 0.81024551, 0.60916971},
    {1.00000000, 0.81957693, 0.62893653}, {1.00000000, 0.82854786, 0.64816570},
    {1.00000000, 0.83717703, 0.66687674}, {1.00000000, 0.84548188, 0.68508786},
    {1.00000000, 0.85347859, 0.70281616}, {1.00000000, 0.86118227, 0.72007777},
    {1.00000000, 0.86860704, 0.73688797}, {1.00000000, 0.87576611, 0.75326132},
    {1.00000000, 0.88267187, 0.76921169}, {1.00000000, 0.88933596, 0.78475236},
    {1.00000000, 0.89576933, 0.79989606}, {1.00000000, 0.90198230, 0.81465502},
    {1.00000000, 0.90963069, 0.82838210}, {1.00000000, 0.91710889, 0.84190889},
    {1.00000000, 0.92441842, 0.85523742}, {1.00000000, 0.93156127, 0.86836903},
    {1.00000000, 0.93853986, 0.88130458}, {1.00000000, 0.94535695, 0.89404470},
    {1.00000000, 0.95201559, 0.90658983}, {1.00000000, 0.95851906, 0.91894041},
    {1.00000000, 0.96487079, 0.93109690}, {1.00000000, 0.97107439, 0.94305985},
    {1.00000000, 0.97713351, 0.95482993}, {1.00000000, 0.98305189, 0.96640795},
    {1.00000000, 0.98883326, 0.97779486}, {1.00000000, 0.99448139, 0.98899179},
    {1.00000000, 1.00000000, 1.00000000}, {0.98947904, 0.99348723, 1.00000000},
    {0.97940448, 0.98722715, 1.00000000}, {0.96975025, 0.98120637, 1.00000000},
    {0.96049223, 0.97541240, 1.00000000}, {0.95160805, 0.96983355, 1.00000000},
    {0.94303638, 0.96443333, 1.00000000}, {0.93480451, 0.95923080, 1.00000000},
    {0.92689056, 0.95421394, 1.00000000}, {0.91927697, 0.94937330, 1.00000000},
    {0.91194747, 0.94470005, 1.00000000}, {0.90488690, 0.94018594, 1.00000000},
    {0.89808115, 0.93582323, 1.00000000}, {0.89151710, 0.93160469, 1.00000000},
    {0.88518247, 0.92752354, 1.00000000}, {0.87906581, 0.92357340, 1.00000000},
    {0.87315640, 0.91974827, 1.00000000}, {0.86744421, 0.91604254, 1.00000000},
    {0.86191983, 0.91245088, 1.00000000}, {0.85657444, 0.90896831, 1.00000000},
    {0.85139976, 0.90559011, 1.00000000}, {0.84638799, 0.90231183, 1.00000000},
    {0.84153180, 0.89912926, 1.00000000}, {0.83682430, 0.89603843, 1.00000000},
    {0.83225897, 0.89303558, 1.00000000}, {0.82782969, 0.89011714, 1.00000000}, /* 9000K */
};

void gamma_adjust::set_gamma(int temp)
{
    assert(3000 <= temp && temp <= 9000);

    int index = (temp - 3000) / 100;
    int index1 = index + (temp == 9000 ? 0 : 1);
    float c = (temp % 100) / 100;

    float adj[3];
    for (int i = 0; i < 3; i++)
        adj[i] = (1 - c) * adjustment[index][i] + c * adjustment[index1][i];

    for (uint32_t i = 0; i < gamma_size; i++) {
        int base_value = i * ((int)UINT16_MAX+1) / gamma_size;

        for (int j = 0; j < 3; j++) {
            ((uint16_t*) gamma_value[j].data)[i] = base_value * adj[j];
        }
    }

    wayfire_shell_set_color_gamma(display.wfshell, output, &gamma_value[0],
            &gamma_value[1], &gamma_value[2]);
}

void gamma_adjust::continuous_change(int from, int to)
{
    if (from != to) {
        int dx = 50 * (to - from) / std::abs(from - to);
        for (int i = from; i * dx < to * dx; i += dx) {
            set_gamma(i);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    set_gamma(to);
}

#define clock std::chrono::system_clock
using pii = std::pair<int, int>;
void gamma_adjust::adjustment_loop()
{
    /* we rely on the OS to clean up our thread */
    int iter = 0;
    while(true) {
        auto tmp = clock::to_time_t(clock::now());
        auto tm2 = std::localtime(&tmp);

        int current = tm2->tm_hour * 60 + tm2->tm_min;

        int target;

        if (current >= day_start && current <= day_end) {
            target = daytime_temp;
        } else {
            target = nighttime_temp;
        }

        if (current_temp != target || iter == 0) {
            continuous_change(current_temp, target);
            current_temp = target;
        } else
        {
            continuous_change(target, target);
        }
        ++iter;

        /* should probably use sleep_until, altough this works just as fine */
        std::this_thread::sleep_for(std::chrono::minutes(1));
    }
}
#undef clock

gamma_adjust::gamma_adjust(uint32_t out, uint32_t sz, wayfire_config *config)
    : output(out), gamma_size(sz)
{
    auto section = config->get_section("shell");
    int h, m;

    std::string s = section->get_string("day_start", "08:00");
    sscanf(s.c_str(), "%d:%d", &h, &m);
    day_start = h * 60 + m;

    s = section->get_string("day_end", "20:00");
    sscanf(s.c_str(), "%d:%d", &h, &m);
    day_end = h * 60 + m;

    daytime_temp = section->get_int("day_temperature", 6500);
    nighttime_temp = section->get_int("night_temperature", 4500);

    for (int i = 0; i < 3; i++) {
        wl_array_init(&gamma_value[i]);
        wl_array_add(&gamma_value[i], gamma_size * sizeof(uint16_t));
    }

    auto fn = std::bind(std::mem_fn(&gamma_adjust::adjustment_loop), this);
    auto th = std::thread(fn);

    /* the adjustment loop is what actually changes color temperature,
     * we separate it from the main thread as it sleeps while inactive */
    th.detach();
}
