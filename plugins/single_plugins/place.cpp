#include <view.hpp>
#include <core.hpp>
#include <workspace-manager.hpp>
#include <signal-definitions.hpp>

class wayfire_place_window : public wayfire_plugin_t
{
    wf::signal_callback_t created_cb;
    wf::signal_callback_t workarea_changed_cb;
    wf_option placement_mode;
    int cascade_x, cascade_y;

    public:
    void init(wayfire_config *config)
    {
        cascade_x = cascade_y = 0;

        created_cb = [=] (wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);

            if (view->role != wf::VIEW_ROLE_TOPLEVEL ||
                view->parent || view->fullscreen ||
                view->maximized)
                return;

            auto workarea = output->workspace->get_workarea();
            auto mode = placement_mode->as_string();

            if (mode == "cascade")
                cascade(view, workarea);
            else if (mode == "random")
                random(view, workarea);
            else
                center(view, workarea);
        };

        workarea_changed_cb = [=] (wf::signal_data_t *data)
        {
            auto workarea = output->workspace->get_workarea();
            if (cascade_x < workarea.x || cascade_x > workarea.x + workarea.width)
                cascade_x = workarea.x;
            if (cascade_y < workarea.y || cascade_y > workarea.y + workarea.height)
                cascade_y = workarea.y;
        };

        auto section = config->get_section("place");
        placement_mode = section->get_option("mode", "center");

        output->connect_signal("reserved-workarea", &workarea_changed_cb);
        output->connect_signal("map-view", &created_cb);
    }

    void cascade(wayfire_view &view, wf_geometry workarea)
    {
        wf_geometry window = view->get_wm_geometry();

        if (cascade_x + window.width > workarea.x + workarea.width ||
            cascade_y + window.height > workarea.y + workarea.height)
        {
            cascade_x = workarea.x;
            cascade_y = workarea.y;
        }

        view->move(cascade_x, cascade_y);

        cascade_x += workarea.width * .03;
        cascade_y += workarea.height * .03;
    }

    void random(wayfire_view &view, wf_geometry workarea)
    {
        wf_geometry window = view->get_wm_geometry();
        wf_geometry area;
        int pos_x, pos_y;

        area.x = workarea.x;
        area.y = workarea.y;
        area.width = workarea.width - window.width;
        area.height = workarea.height - window.height;

        if (area.width < 0 || area.height < 0)
        {
            center(view, workarea);
            return;
        }

        pos_x = rand() % area.width + area.x;
        pos_y = rand() % area.height + area.y;

        view->move(pos_x, pos_y);

    }

    void center(wayfire_view &view, wf_geometry workarea)
    {
        wf_geometry window = view->get_wm_geometry();
        window.x = workarea.x + (workarea.width / 2) - (window.width / 2);
        window.y = workarea.y + (workarea.height / 2) - (window.height / 2);
        view->move(window.x, window.y);
    }

    void fini()
    {
        output->disconnect_signal("reserved-workarea", &workarea_changed_cb);
        output->disconnect_signal("map-view", &created_cb);
    }
};

extern "C"
{
    wayfire_plugin_t *newInstance()
    {
        return new wayfire_place_window();
    }
}
