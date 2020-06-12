#include <wayfire/plugin.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/core.hpp>

#include <wayfire/view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>

#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-manager.hpp>

#include <wayfire/util/duration.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/nonstd/reverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <linux/input-event-codes.h>

#include <algorithm>
#include <exception>
#include <set>

constexpr const char* scale_transformer = "scale";
constexpr const char* scale_transformer_background = "scale";
constexpr float background_dim_factor = 0.6;

using namespace wf::animation;
class ScalePaintAttribs
{
  public:
    ScalePaintAttribs(const duration_t& duration)
        : scale(duration, 1, 1), off_x(duration, 0, 0),
        off_y(duration, 0, 0), alpha(duration, 1, 1) { }

    timed_transition_t scale;
    timed_transition_t off_x, off_y;
    timed_transition_t alpha;
};
/*
enum SwitcherViewPosition
{
    SWITCHER_POSITION_LEFT = 0,
    SWITCHER_POSITION_CENTER = 1,
    SWITCHER_POSITION_RIGHT = 2
};

static constexpr bool view_expired(int view_position)
{
    return view_position < SWITCHER_POSITION_LEFT ||
        view_position > SWITCHER_POSITION_RIGHT;
}
*/
struct ScaleView
{
    wayfire_view view;
    ScalePaintAttribs attribs;
    wf::geometry_t box; /* location bounding box */
    /* TODO: position in grid */
    /* flag to exclude views from rendering that do not have
     * their size set yet */
	bool exclude_render;
	/* position in the rows and columns */
	int row;
	int col;
	
    //~ int position;
    ScaleView(duration_t& duration) : attribs(duration),
		exclude_render(true), row(0), col(0) {}

    /* Make animation start values the current progress of duration */
    void refresh_start()
    {
        for_each([] (timed_transition_t& t) { t.restart_same_end(); });
    }

    void to_end()
    {
        for_each([] (timed_transition_t& t) { t.set(t.end, t.end); });
    }

  private:
    void for_each(std::function<void(timed_transition_t& t)> call)
    {
        call(attribs.off_x);
        call(attribs.off_y);

        call(attribs.scale);
        call(attribs.alpha);
    }
};

class WayfireScale : public wf::plugin_interface_t
{
    //~ wf::option_wrapper_t<double> touch_sensitivity{"scale/touch_sensitivity"};
    wf::option_wrapper_t<int> speed{"scale/speed"};
    //~ wf::option_wrapper_t<bool> middle_click_close{"scale/middle_click_close"};
    wf::option_wrapper_t<int> spacing{"scale/spacing"};
	wf::option_wrapper_t<double> inactive_alpha{"scale/inactive_alpha"};
	
    duration_t duration{speed};
    duration_t background_dim_duration{speed};
    timed_transition_t background_dim{background_dim_duration};

    /* Keep track of views */
    std::vector<ScaleView> views;
    /* organize views into rows and columns
     * we hold indexes into the previous array here */
    std::vector<std::vector<size_t> > grid;
    

    // the modifiers which were used to activate switcher
    uint32_t activating_modifiers = 0;
    /* index of currently active view in views */
    size_t active_view = 0;
    bool active = false;
    /* keep track of which view is under the mouse */
    bool have_mouse_view = false;
    size_t mouse_view = 0;
    /* keep track which view is "selected" by the keyboard */
    size_t selected_view = 0;
    
    public:

    void init() override
    {
        grab_interface->name = "scale";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR;

        output->add_key(
            wf::option_wrapper_t<wf::keybinding_t>{"scale/initiate"},
            &initiate_cb);
        output->add_key(
            wf::option_wrapper_t<wf::keybinding_t>{"scale/initiate_all"},
            &initiate_all_cb);
        output->connect_signal("detach-view", &view_removed);
        output->connect_signal("layer-attach-view", &view_added);
        output->connect_signal("focus-view", &focus_changed);

		grab_interface->callbacks.keyboard.key = [=] (uint32_t key, uint32_t state) {
			if(state == WLR_KEY_PRESSED) switch(key) {
				case KEY_ESC:
					dearrange();
					break;
				case KEY_ENTER:
					keyboard_switch();
					break;
				case KEY_UP:
				case KEY_DOWN:
				case KEY_LEFT:
				case KEY_RIGHT:
					handle_key_move(key);
					break;
			}
		};
		
		grab_interface->callbacks.pointer.motion = [=] (int32_t x, int32_t y) {
			handle_motion(x, y);
		};
		
		grab_interface->callbacks.pointer.button = [=] (uint32_t button, uint32_t state) {
			if(state == WLR_BUTTON_PRESSED) {
				if(button == BTN_LEFT) handle_left_click();
			}
		};
		
		/* TODO: rest of grabs
        grab_interface->callbacks.keyboard.mod = [=] (uint32_t mod, uint32_t state)
        {
            if (state == WLR_KEY_RELEASED && (mod & activating_modifiers))
                handle_done();
        };

        grab_interface->callbacks.touch.down = [=] (int id, int x, int y) {
            if (id == 0) handle_touch_down(x, y);
        };

        grab_interface->callbacks.touch.up = [=] (int id) {
            if (id == 0) handle_touch_up();
        };

        grab_interface->callbacks.touch.motion = [=] (int id, int x, int y) {
            if (id == 0) handle_touch_motion(x, y);
        };
		*/
        grab_interface->callbacks.cancel = [=] () {deinit_switcher();};
    }

    wf::key_callback initiate_cb = [=] (uint32_t)
    {
        return init_switcher();
    };

    wf::key_callback initiate_all_cb = [=] (uint32_t)
    {
        return init_switcher();
    };


    wf::effect_hook_t damage = [=] ()
    {
        if(duration.running())
			output->render->damage_whole();
    };

    wf::signal_callback_t view_removed = [=] (wf::signal_data_t *data)
    {
        handle_view_removed(get_signaled_view(data));
    };
    
    wf::signal_callback_t view_added = [=] (wf::signal_data_t* data)
    {
		handle_view_added(get_signaled_view(data));
	};
	
	wf::signal_callback_t view_geom_changed = [=] (wf::signal_data_t* data) {
		view_geometry_changed_signal* d2 = (view_geometry_changed_signal*)data;
		auto view = d2->view;
		auto geom = d2->old_geometry;
		handle_view_geom_changed(view, geom);
	};
	
	wf::signal_callback_t focus_changed = [=] (wf::signal_data_t* data) {
		handle_focus_changed(get_signaled_view(data));
	};

    void handle_view_removed(wayfire_view view)
    {
        // not running at all, don't care
        if (!output->is_plugin_active(grab_interface->name))
            return;

        bool need_action = false;
        wayfire_view new_active = output->get_active_view();
        for(auto it = views.begin(); it != views.end(); ) {
			if(it->view == view) {
				need_action = true;
				it = views.erase(it);
			}
			else {
				if(it->view == new_active)
					active_view = it - views.begin();
				++it;
			}
		}

        // don't do anything if we're not using this view
        if (!need_action)
            return;

        if (active) {
            rearrange();
        }
    }
    
    void handle_view_added(wayfire_view view) {
        // not running at all, don't care
        if (!output->is_plugin_active(grab_interface->name))
            return;
        if(!active) return;
        auto bbox = view->get_bounding_box();
        fprintf(stderr, "view added: %p\nsize: %d, %d\n", view.get(), 
			bbox.width, bbox.height);
		/* TODO: set up handler for geometry change?
		 * currently, render() checks that the view does not grow too big,
		 * but not if it shrinks
		view->connect_signal("geometry-changed", &view_geom_changed); */
		uint32_t layer = output->workspace->get_view_layer(view);
		switch(layer) {
			case wf::WM_LAYERS:
			case wf::LAYER_MINIMIZED:
				views.push_back(create_scale_view(view));
				active_view = views.size() - 1;
				rearrange();
				break;
			case wf::BELOW_LAYERS:
			case wf::ABOVE_LAYERS:
				/* we do nothing */
				break;
			case 0:
				LOGW("New view on unknown layer");
				break;
			default:
				LOGI("New view on layer that is not processed by scale\n");
				break;
		}
	}

	void handle_view_geom_changed(wayfire_view view, wf::geometry_t& geom) {
		/* only for debug purposes */
		fprintf(stderr, "view geometry changed: %p\n", view.get());
		fprintf(stderr, "old geometry: %d, %d, %d, %d\n", geom.x,
			geom.y, geom.width, geom.height);
		auto geom2 = view->get_bounding_box(scale_transformer);
		fprintf(stderr, "new geometry: %d, %d, %d, %d\n", geom2.x,
			geom2.y, geom2.width, geom2.height);
/*		geom2 = view->get_untransformed_bounding_box();
		fprintf(stderr, "untransformed: %d, %d, %d, %d\n", geom2.x,
			geom2.y, geom2.width, geom2.height);
	*/	
	}
	
	void handle_focus_changed(wayfire_view view) {
		if (!output->is_plugin_active(grab_interface->name))
            return;
		for(size_t i = 0; i < views.size(); i++)
			if(views[i].view == view) {
				active_view = i;
				break;
			}
	}
	
	void handle_motion(int32_t x, int32_t y) {
		if (!output->is_plugin_active(grab_interface->name))
            return;
        if(!active) return;
        /* find the view that is under the mouse */
        have_mouse_view = find_view(mouse_view, x, y);
        if(have_mouse_view) {
			if(mouse_view != selected_view) {
				views[selected_view].attribs.alpha.restart_with_end(inactive_alpha);
				selected_view = mouse_view;
				views[mouse_view].attribs.alpha.restart_with_end(1.0);
				duration.start();
			}
		}
	}
	
	void handle_left_click() {
		if(have_mouse_view) {
			active_view = mouse_view;
			output->focus_view(views[mouse_view].view, true);
			dearrange();
		}
	}
	
	/* find view at the given transformed position */
	bool find_view(size_t& id, int32_t x, int32_t y) {
		for(size_t i = 0; i < views.size(); i++) {
			if(x >= views[i].box.x && x <= views[i].box.x + views[i].box.width &&
				y >= views[i].box.y && y <= views[i].box.y + views[i].box.height) {
					id = i;
					return true;
				}
		}
		return false;
	}
	
	/* switch to view based on keyboard selection */
	void keyboard_switch() {
		active_view = selected_view;
		output->focus_view(views[active_view].view, true);
		dearrange();
	}
	
	void handle_key_move(uint32_t key) {
		if(!output->is_plugin_active(grab_interface->name)) return;
        if(!active) return;
		if(selected_view >= views.size()) return;
		int row = views[selected_view].row;
		int col = views[selected_view].col;
		switch(key) {
			case KEY_UP:
				row--;
				break;
			case KEY_DOWN:
				row++;
				break;
			case KEY_LEFT:
				col--;
				break;
			case KEY_RIGHT:
				col++;
				break;
		}
		/* TODO: wrap around? This could be an option */
		if(row < 0 || col < 0) return;
		if((size_t)row >= grid.size()) return;
		if((size_t)col >= grid[row].size()) {
			if(key == KEY_RIGHT) return;
			col = grid[row].size() - 1;
		}
		
		views[selected_view].attribs.alpha.restart_with_end(inactive_alpha);
		selected_view = grid[row][col];
		views[selected_view].attribs.alpha.restart_with_end(1.0);
		duration.start();
	}

    /* Sets up basic hooks needed while switcher works and/or displays animations.
     * Also lower any fullscreen views that are active */
    bool init_switcher()
    {
        if (!output->activate_plugin(grab_interface))
            return false;
		if(!grab_interface->grab()) {
			output->deactivate_plugin(grab_interface);
			return false;
		}
        output->render->add_effect(&damage, wf::OUTPUT_EFFECT_PRE);
        output->render->set_renderer(scale_renderer);
        output->render->set_redraw_always();
        active = arrange();
        if(!active) deinit_switcher();
        return active;
    }

    /* The reverse of init_switcher */
    void deinit_switcher()
    {
		if(grab_interface->is_grabbed()) grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);

        output->render->rem_effect(&damage);
        output->render->set_renderer(nullptr);
        output->render->set_redraw_always(false);

        for (auto& view : output->workspace->get_views_in_layer(wf::ALL_LAYERS))
        {
            view->pop_transformer(scale_transformer);
            view->pop_transformer(scale_transformer_background);
        }

        views.clear();
        active_view = 0;
        mouse_view = 0;
        have_mouse_view = false;
        selected_view = 0;
    }

    /* offset from the left or from the right 
    float get_center_offset()
    {
        return output->get_relative_geometry().width / 3;
    }*/

    /* Move view animation target to intended position */
    void move(ScaleView& sv, const wf::geometry_t& box)
    {
		const auto& view_box = sv.view->get_bounding_box(scale_transformer);
		sv.box = box;
		if(view_box.width > 0 && view_box.height > 0) {
			set_view_transforms(sv);
			sv.exclude_render = false;
		}
	}
	
	void set_view_transforms(ScaleView& sv) {
		auto& box = sv.box;
		const auto& view_box = sv.view->get_bounding_box(scale_transformer);
		double w_scale = box.width / (double)view_box.width;
		double h_scale = box.height / (double)view_box.height;
		double new_scale = w_scale < h_scale ? w_scale : h_scale;
		if(new_scale > 1.0) new_scale = 1.0;
		
		/* ensure that the box stored actually fits the view */
		int new_width = (int)ceil(view_box.width * new_scale);
		int new_height = (int)ceil(view_box.height * new_scale);
		if(new_width < box.width) {
			box.x += (box.width - new_width) / 2;
			box.width = new_width;
		}
		if(new_height < box.height) {
			box.y += (box.height - new_height) / 2;
			box.height = new_height;
		}
		
		sv.attribs.scale.restart_with_end(new_scale);
		/* we need to add offset based on the center positions
		 * (to take into account scaling, the transform is based
		 * on the center) */
		double center_x1 = box.x + box.width / 2.0;
		double center_y1 = box.y + box.height / 2.0;
		double center_x2 = view_box.x + view_box.width / 2.0;
		double center_y2 = view_box.y + view_box.height / 2.0;
		sv.attribs.off_x.restart_with_end(center_x1 - center_x2);
		sv.attribs.off_y.restart_with_end(center_y1 - center_y2);
		
		
		/* TODO: alpha -- maybe not here, but with selection?
        sv.attribs.alpha.restart_with_end(
            view_expired(sv.position) ? 0.3 : 1.0); */
    }

    
    /* Calculate alpha for the view when switcher is inactive. */
    float get_view_normal_alpha(wayfire_view view)
    {
        /* Usually views are visible, but if they were minimized,
         * and we aren't restoring the view, it has target alpha 0.0 */
        if (view->minimized && (views.empty() || view != views[0].view))
            return 0.0;
        return 1.0;
    }

    // returns a list of mapped views
    std::vector<wayfire_view> get_workspace_views() const
    {
        auto all_views = output->workspace->get_views_on_workspace(
            output->workspace->get_current_workspace(),
            wf::WM_LAYERS | wf::LAYER_MINIMIZED, true);

        decltype(all_views) mapped_views;
        for (auto view : all_views)
        {
            if (view->is_mapped())
                mapped_views.push_back(view);
        }

        return mapped_views;
    }

    /* Create the initial arrangement on the screen
     * Returns false if there are no views to arrange (in this case,
     * we might exit the switcher) */
    bool arrange()
    {
        // clear views in case that deinit() hasn't been run
        views.clear();
        
        background_dim.set(1, background_dim_factor);
        background_dim_duration.start();

        std::vector<wayfire_view> ws_views = get_workspace_views();
        active_view = 0;
        for (auto v : ws_views)
            views.push_back(create_scale_view(v));
        return rearrange();
	}
	
	/* rearrange the views already present in views */
	bool rearrange() {
        /* find the position for all views -- adapted from compiz */
        int nWindows = views.size();
		if(!nWindows) return false;

		int lines = (int)floor(sqrt((double)nWindows + 1));
		int nSlots  = 0;
		
		auto screen_size = output->get_screen_size();
		
		int yoffset = (int)floor(0.1 * screen_size.height); /* TODO: this as option */
		int yoffsetb = yoffset; /* TODO: different offset at bottom */
		int xoffset = (int)floor(0.1 * screen_size.width);
		/* height of windows in one row */
		int height = (screen_size.height - yoffset - yoffsetb - (lines + 1) * spacing) / lines;
		if(height <= 0) { LOGW("height <= 0!"); height = 20; }
		
		grid.clear();
		grid.reserve(lines);
		
		/* y coordinate of current row */
		int y = yoffset + spacing;
		for (int i = 0; i < lines; i++) {
			int n = std::min(nWindows - nSlots, (int)ceilf((float) nWindows / lines));
			int x = xoffset + spacing;
			/* width of one window */
			int width = (screen_size.width - xoffset - (n + 1) * spacing) / n;
			if(width <= 0) { LOGW("width <= 0!"); width = 20; }
			std::vector<size_t> row;
			row.reserve(n);
			for (int j = 0; j < n; j++) {
				//~ fprintf(stderr,"nslots: %d, x: %d, y: %d, width: %d, height %d\n", nSlots, x, y, width, height);
				move(views[nSlots], wf::geometry_t{x, y, width, height});
				views[nSlots].row = i;
				views[nSlots].col = j;
				row.push_back(nSlots);
				
				if((size_t)nSlots == active_view) views[nSlots].attribs.alpha.restart_with_end(1.0);
				else views[nSlots].attribs.alpha.restart_with_end(inactive_alpha);
				x += width + spacing;
				nSlots++;
			}
			y += height + spacing;
			grid.push_back(std::move(row));
		}
		
		duration.start();
		return true;
    }

    void dearrange()
    {
        for (auto& sv : views)
        {
            sv.attribs.off_x.restart_with_end(0);
            sv.attribs.off_y.restart_with_end(0);

            sv.attribs.scale.restart_with_end(1.0);

            sv.attribs.alpha.restart_with_end(1.0);
            //~ sv.attribs.alpha.restart_with_end(get_view_normal_alpha(sv.view));
			/*
            if (sv.view == fading_view)
            {
                sv.attribs.alpha.end = 0.0;
                // make sure we don't fade out the other unfocused view instance as well
                fading_view = nullptr;
            }
            */
        }

        background_dim.restart_with_end(1);
        background_dim_duration.start();
        duration.start();
        active = false;

        /* Potentially restore view[0] if it was maximized -- TODO
        if (views.size())
            output->focus_view(views[0].view, true); */
    }

    std::vector<wayfire_view> get_background_views() const
    {
        return output->workspace->get_views_on_workspace(
            output->workspace->get_current_workspace(), wf::BELOW_LAYERS, false);
    }

    std::vector<wayfire_view> get_overlay_views() const
    {
        return output->workspace->get_views_on_workspace(
            output->workspace->get_current_workspace(), wf::ABOVE_LAYERS, false);
    }

    void dim_background(float dim)
    {
        for (auto view : get_background_views())
        {
            if (dim == 1.0)
            {
                view->pop_transformer(scale_transformer_background);
            } else
            {
                if (!view->get_transformer(scale_transformer_background))
                {
                    view->add_transformer(std::make_unique<wf::view_2D> (view),
                        scale_transformer_background);
                }

                auto tr = dynamic_cast<wf::view_2D*> (
                    view->get_transformer(scale_transformer_background).get());
                tr->alpha = dim;
            }
        }
    }

    ScaleView create_scale_view(wayfire_view view)
    {
        /* we add a view transform if there isn't any.
         *
         * Note that a view might be visible on more than 1 place, so damage
         * tracking doesn't work reliably. To circumvent this, we simply damage
         * the whole output -- TODO: this might not be needed here */
        if (!view->get_transformer(scale_transformer))
        {
            view->add_transformer(std::make_unique<wf::view_2D> (view),
                scale_transformer);
        }

        ScaleView sw{duration};
        sw.view = view;
        //~ sw.position = SWITCHER_POSITION_CENTER;
        return sw;
    }

    void render_view(ScaleView& sv, const wf::framebuffer_t& fb)
    {
		const auto& view_box = sv.view->get_bounding_box();
        if(sv.exclude_render) {
			if(view_box.width > 0 && view_box.height > 0) {
				set_view_transforms(sv);
				sv.exclude_render = false;
			}
			else return;
		}
		
		/* check that scale is still consistent -- only if we're still active */
		if(active) {
			double scale = sv.attribs.scale;
			if(view_box.width * scale > sv.box.width + 1 ||
					view_box.height * scale > sv.box.height + 1) {
				//~ LOGW("view has too large scale");
				set_view_transforms(sv);
			}
		}
        
        auto transform = dynamic_cast<wf::view_2D*> (
            sv.view->get_transformer(scale_transformer).get());
        assert(transform);
        transform->scale_x = (double)sv.attribs.scale;
        transform->scale_y = (double)sv.attribs.scale;
        transform->angle = 0.0;
        transform->translation_x = (double)sv.attribs.off_x;
        transform->translation_y = (double)sv.attribs.off_y;
        transform->alpha = (double)sv.attribs.alpha;

        sv.view->render_transformed(fb, fb.geometry);
        
        transform->scale_x = 1.0;
        transform->scale_y = 1.0;
        transform->translation_x = 0.0;
        transform->translation_y = 0.0;
        
        /* if animation ended, update durations so that any next
         * animation starts from the current state */
        if(!duration.running()) sv.to_end();
    }

    wf::render_hook_t scale_renderer = [=] (const wf::framebuffer_t& fb)
    {
        OpenGL::render_begin(fb);
        OpenGL::clear({0, 0, 0, 1});
        OpenGL::render_end();

        dim_background(background_dim);
        for (auto view : get_background_views())
            view->render_transformed(fb, fb.geometry);

        /* Render in the reverse order because we don't use depth testing */
        for(size_t i = 0; i < views.size(); i++) {
			size_t j = views.size() - i - 1;
			if(j != active_view) render_view(views[j], fb);
		}
		/* render the active view last, so it will be on top
		 * (when arranging back) */
        if(active_view < views.size())
			render_view(views[active_view], fb);

        for (auto view : get_overlay_views())
            view->render_transformed(fb, fb.geometry);

        if (!duration.running())
        {
//            cleanup_expired();

            if (!active)
                deinit_switcher();
        }
    };

    /* delete all views matching the given criteria, skipping the first "start" views */
    void cleanup_views(std::function<bool(ScaleView&)> criteria)
    {
        auto it = views.begin();
        while(it != views.end())
        {
            if (criteria(*it)) {
                it = views.erase(it);
            } else {
                ++it;
            }
        }
    }

    /* Removes all expired views from the list 
    void cleanup_expired()
    {
        cleanup_views([=] (ScaleView& sv)
            { return view_expired(sv.position); });
    }*/


    void fini() override
    {
        if (output->is_plugin_active(grab_interface->name))
            deinit_switcher();
        output->rem_binding(&initiate_cb);
        output->rem_binding(&initiate_all_cb);
        output->disconnect_signal("detach-view", &view_removed);
    }
};

DECLARE_WAYFIRE_PLUGIN(WayfireScale);
