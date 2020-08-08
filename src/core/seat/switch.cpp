#include "switch.hpp"
#include <wayfire/signal-definitions.hpp>
#include <wayfire/core.hpp>

wf::switch_device_t::switch_device_t(wlr_input_device *dev) :
    wf_input_device_internal(dev)
{
    on_switch.set_callback([&] (void *data)
    {
        this->handle_switched((wlr_event_switch_toggle*)data);
    });
    on_switch.connect(&dev->switch_device->events.toggle);
}

void wf::switch_device_t::handle_switched(wlr_event_switch_toggle *ev)
{
    wf::switch_signal data;
    data.device = nonstd::make_observer(this);
    data.state  = (ev->switch_state == WLR_SWITCH_STATE_ON);

    std::string event_name;
    switch (ev->switch_type)
    {
      case WLR_SWITCH_TYPE_TABLET_MODE:
        event_name = "tablet-mode";
        break;

      case WLR_SWITCH_TYPE_LID:
        event_name = "lid-state";
        break;
    }

    wf::get_core().emit_signal(event_name, &data);
}
