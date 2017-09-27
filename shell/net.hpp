#ifndef NET_HPP
#define NET_HPP

#include "widgets.hpp"
#include <thread>
#include <mutex>

struct connection_info
{
    std::string name;
    int strength; /* in percentage, for Wi-Fi/Broadband */
    std::string icon;

    bool updated;

    std::mutex mutex;
};

struct network_provider_backend;

struct network_widget : public widget
{
    private:
    std::thread updater_thread;
    connection_info connection;
    network_provider_backend *backend = nullptr;

    public:
    void create();
    int  get_width() { return width; };
    bool update();
    bool resized();
    void repaint();
};

#endif /* end of include guard: NET_HPP */
