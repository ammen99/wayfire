#include "output.hpp"
#include "plugin-loader.hpp"

#include <unordered_map>
#include <unordered_set>
#include <nonstd/safe-list.hpp>

namespace wf
{
class output_impl_t : public output_t
{
  private:
    std::unordered_multiset<wayfire_grab_interface> active_plugins;
    std::unique_ptr<plugin_manager> plugin;

    wayfire_view active_view, last_active_toplevel;
    signal_callback_t view_disappeared_cb;

  public:
    output_impl_t(wlr_output *output);
    virtual ~output_impl_t();

    /**
     * Implementations of the public APIs
     */
    virtual bool activate_plugin(wayfire_grab_interface owner);
    virtual bool deactivate_plugin(wayfire_grab_interface owner);
    virtual bool is_plugin_active(owner_t owner_name) const;
    virtual wayfire_view get_active_view() const;
    virtual void set_active_view(wayfire_view v);

    /**
     * Cancel all active grab interfaces.
     */
    void break_active_plugins();

    /**
     * @return The currently active input grab interface, or nullptr if none
     */
    wayfire_grab_interface get_input_grab_interface();
};
}

