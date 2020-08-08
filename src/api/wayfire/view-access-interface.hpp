#pragma once

#include "wayfire/condition/access_interface.hpp"
#include "wayfire/view.hpp"
#include <string>
#include <tuple>

namespace wf
{
/**
 * @brief The view_access_interface_t class is a view specific implementation of
 * access_interface_t.
 *
 * Refer to the docs of access_interface_t for more information.
 *
 * The following properties are supported:
 *
 * format:
 * property -> type (comment)
 *
 * "app_id" -> std::string
 * "title" -> std::string
 * "role" -> std::string
 * "fullscreen" -> bool
 * "activated" -> bool
 * "minimized" -> bool
 * "tiled-left" -> bool
 * "tiled-right" -> bool
 * "tiled-top" -> bool
 * "tiled-bottom" -> bool
 * "maximized" -> bool
 * "floating" -> bool
 * "type" -> std::string (This will return a type string like the matcher plugin did)
 */
class view_access_interface_t : public access_interface_t
{
  public:
    /**
     * @brief view_access_interface_t Default constructor.
     */
    view_access_interface_t();

    /**
     * @brief view_access_interface_t Constructor that immediately assigns a view.
     *
     * @param[in] view The view to assign.
     */
    view_access_interface_t(wayfire_view view);

    // Inherits docs.
    virtual ~view_access_interface_t() override;

    // Inherits docs.
    virtual variant_t get(const std::string & identifier, bool & error) override;

    /**
     * @brief set_view Setter for the view to interrogate.
     *
     * @param[in] view The view to assign.
     */
    void set_view(wayfire_view view);

  private:

    /**
     * @brief _view The view to interrogate.
     */
    wayfire_view _view;
};
} // End namespace wf.
