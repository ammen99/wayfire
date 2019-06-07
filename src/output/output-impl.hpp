#include "output.hpp"
#include "plugin-loader.hpp"

#include <unordered_set>
#include <nonstd/safe-list.hpp>

namespace wf
{
class output_impl_t : public output_t
{
  private:
    std::unordered_multiset<wf::plugin_grab_interface_t*> active_plugins;
    std::unique_ptr<plugin_manager> plugin;

    wayfire_view active_view, last_active_toplevel;
    signal_callback_t view_disappeared_cb;

  public:
    output_impl_t(wlr_output *output);
    virtual ~output_impl_t();

    /**
     * Implementations of the public APIs
     */
    bool activate_plugin(const plugin_grab_interface_uptr& owner) override;
    bool deactivate_plugin(const plugin_grab_interface_uptr& owner) override;
    bool is_plugin_active(std::string owner_name) const override;
    wayfire_view get_active_view() const override;
    void set_active_view(wayfire_view v) override;

    /**
     * Cancel all active grab interfaces.
     */
    void break_active_plugins();

    /**
     * @return The currently active input grab interface, or nullptr if none
     */
    plugin_grab_interface_t* get_input_grab_interface();
};
}

