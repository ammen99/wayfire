#ifndef XDG_SHELL_HPP
#define XDG_SHELL_HPP

#include "view-impl.hpp"
extern "C"
{
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
}

/**
 * A class for xdg-shell(and -v6) popups. XdgPopupVersion can be either a
 * `wlr_xdg_popup` or `wlr_xdg_popup_v6`.
 */
template<class XdgPopupVersion>
class wayfire_xdg_popup : public wf::wlr_child_surface_base_t
{
  protected:
    wf::wl_listener_wrapper on_destroy, on_new_popup, on_map, on_unmap;

    XdgPopupVersion *popup;

    void unconstrain();
    void _do_unconstrain(wlr_box box);

  public:
    wayfire_xdg_popup(XdgPopupVersion *popup);
    ~wayfire_xdg_popup();

    virtual wf_point get_offset() override;
    virtual wf_point get_window_offset() override;
    void send_done();
};

template<class XdgPopupVersion>
void create_xdg_popup(XdgPopupVersion *popup);
template<>
void create_xdg_popup<wlr_xdg_popup>(wlr_xdg_popup* popup);

template<class XdgToplevelVersion>
class wayfire_xdg_view : public wf::wlr_view_t
{
  private:
    wf::wl_listener_wrapper on_map, on_unmap, on_destroy, on_new_popup,
                            on_request_move, on_request_resize,
                            on_request_minimize, on_request_maximize,
                            on_request_fullscreen, on_set_parent,
                            on_set_title, on_set_app_id;

    wf_point xdg_surface_offset = {0, 0};
    XdgToplevelVersion *xdg_toplevel;

  public:
    wayfire_xdg_view(XdgToplevelVersion *toplevel);
    virtual ~wayfire_xdg_view();

    void map(wlr_surface *surface) final;
    void commit() final;

    wf_point get_window_offset() final;
    wf_geometry get_wm_geometry() final;

    void set_tiled(uint32_t edges) final;
    void set_activated(bool act) final;
    void _set_activated(bool act);
    void set_maximized(bool max) final;
    void set_fullscreen(bool full) final;

    void resize(int w, int h) final;
    void request_native_size()override final;

    void destroy() final;
    void close() final;
};

#endif /* end of include guard: XDG_SHELL_HPP */
