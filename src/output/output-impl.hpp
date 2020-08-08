#include "wayfire/output.hpp"
#include "plugin-loader.hpp"

#include <unordered_set>
#include <wayfire/nonstd/safe-list.hpp>

namespace wf
{
class output_impl_t : public output_t
{
  private:
    std::unordered_multiset<wf::plugin_grab_interface_t*> active_plugins;
    std::unique_ptr<plugin_manager> plugin;

    signal_callback_t view_disappeared_cb;
    bool inhibited = false;

    enum focus_view_flags_t
    {
        /* Raise the view which is being focused */
        FOCUS_VIEW_RAISE        = (1 << 0),
        /* Close popups of non-focused views */
        FOCUS_VIEW_CLOSE_POPUPS = (1 << 1),
    };

    /**
     * Set the given view as the active view.
     * If the output has focus, try to focus the view as well.
     *
     * @param flags bitmask of @focus_view_flags_t
     */
    void update_active_view(wayfire_view view, uint32_t flags);

    /**
     * Close all popups on the output which do not belong to the active view.
     */
    void close_popups();

    /** @param flags bitmask of @focus_view_flags_t */
    void focus_view(wayfire_view view, uint32_t flags);

    wf::dimensions_t effective_size;

  public:
    output_impl_t(wlr_output *output, const wf::dimensions_t& effective_size);
    /**
     * Start all the plugins on this output.
     */
    void start_plugins();

    virtual ~output_impl_t();
    wayfire_view active_view;

    /**
     * Implementations of the public APIs
     */
    bool can_activate_plugin(const plugin_grab_interface_uptr& owner,
        uint32_t flags = 0) override;
    bool activate_plugin(const plugin_grab_interface_uptr& owner,
        uint32_t flags = 0) override;
    bool deactivate_plugin(const plugin_grab_interface_uptr& owner) override;
    bool is_plugin_active(std::string owner_name) const override;
    wayfire_view get_active_view() const override;
    void focus_view(wayfire_view v, bool raise) override;
    void refocus(wayfire_view skip_view, uint32_t layers) override;
    wf::dimensions_t get_screen_size() const override;

    /**
     * Set the output as inhibited, so that no plugins can be activated
     * except those that ignore inhibitions.
     */
    void inhibit_plugins();

    /**
     * Uninhibit the output.
     */
    void uninhibit_plugins();

    /** @return true if the output is inhibited */
    bool is_inhibited() const;

    /**
     * @return The currently active input grab interface, or nullptr if none
     */
    plugin_grab_interface_t *get_input_grab_interface();

    /** Set the effective resolution of the output */
    void set_effective_size(const wf::dimensions_t& size);
};
}
