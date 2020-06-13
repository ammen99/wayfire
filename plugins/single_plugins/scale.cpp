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

struct ScaleView
{
	wayfire_view view;
	ScalePaintAttribs attribs;
	wf::geometry_t box; /* location bounding box of scaled view */
	/* box available for this view; this is not filled completely,
	 * since the aspect ratio will not necessarily fit the view */
	wf::geometry_t requested_box;
	/* flag to exclude views from rendering that do not have
	 * their size set yet */
	bool exclude_render;
	/* position in the rows and columns */
	int row;
	int col;
	
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
	wf::option_wrapper_t<int> speed{"scale/speed"};
	//~ wf::option_wrapper_t<bool> middle_click_close{"scale/middle_click_close"};
	wf::option_wrapper_t<int> spacing{"scale/spacing"};
	wf::option_wrapper_t<double> inactive_alpha{"scale/inactive_alpha"};
	wf::option_wrapper_t<int> switch_thresh{"scale/workspace_switch_threshold"};
	
	duration_t duration{speed};
	duration_t background_dim_duration{speed};
	timed_transition_t background_dim{background_dim_duration};

	/* Keep track of views */
	std::vector<ScaleView> views;
	/* organize views into rows and columns
	 * we hold indexes into the previous array here */
	std::vector<std::vector<size_t> > grid;
	
	/* index of currently active view in views */
	size_t active_view = 0;
	/* flag if we are active: it can be false while an
	 * animation is still happening */
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
		grab_interface->capabilities = wf::CAPABILITY_GRAB_INPUT;

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
		grab_interface->callbacks.cancel = [=] () {deinit_switcher();};
	}

	wf::key_callback initiate_cb = [=] (uint32_t)
	{
		return init_switcher(false);
	};

	wf::key_callback initiate_all_cb = [=] (uint32_t)
	{
		return init_switcher(true);
	};
	
	/* main logic: update transforms of all windows */
	wf::effect_hook_t pre_render = [=] ()
	{
		if(duration.running()) {
			update_transforms();
			output->render->damage_whole();
		}
		else {
			if(!active) deinit_switcher();
			/* update the state of duration attributes */
			for(auto& sv : views) sv.to_end();
		}
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

		if (active)
			rearrange();
	}
	
	void handle_view_added(wayfire_view view) {
		// not running at all, don't care
		if (!output->is_plugin_active(grab_interface->name))
			return;
		if(!active) return;
		uint32_t layer = output->workspace->get_view_layer(view);
		switch(layer) {
			case wf::WM_LAYERS:
			case wf::LAYER_MINIMIZED:
				/* set up handler for geometry change
				 * TODO: more checks that this works properly */
				view->connect_signal("geometry-changed", &view_geom_changed);
				views.push_back(create_scale_view(view));
				/* we assume that the new view becomes the active one */
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
		for(auto& sv : views) {
			if(sv.view == view) set_view_transforms(sv);
			if(!duration.running() && active) duration.start();
		}
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
				if(!duration.running()) duration.start();
			}
		}
	}
	
	void switch_to_active() {
		output->focus_view(views[active_view].view, true);
		dearrange();
		/* switch to the workspace of the selected view if needed */
		wf::point_t ws = output->workspace->get_current_workspace();
		double area = get_view_workspace_rel_area(views[active_view].view, ws);
		fprintf(stderr,"switch_to_active(): ws: (%d, %d), area: %f\n",
			ws.x, ws.y, area);
		if(area < 0.5 && area < switch_thresh / 100.0) {
			/* we might need to change to a different workspace
			 * note: if the overlap is above 0.5, then this is the
			 * workspace with the largest share of the view's area */
			wf::point_t target_ws = get_view_main_workspace(views[active_view].view);
			fprintf(stderr,"switch_to_active(): target_ws(): (%d, %d)\n",
				target_ws.x, target_ws.y);
			/* request_workspace() does not work (vswitch plugin is incompatible with this one) */
			if(target_ws != ws) output->workspace->request_workspace(target_ws);
			//~ if(target_ws != ws) output->workspace->set_workspace(target_ws);
		}
	}
	
	void handle_left_click() {
		if(have_mouse_view) {
			active_view = mouse_view;
			switch_to_active();
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
		switch_to_active();
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
		if(!duration.running()) duration.start();
	}

	/* Sets up basic hooks needed while switcher works 
	 * and arrange active views */
	bool init_switcher(bool all_workspaces = false) {
		if (!output->activate_plugin(grab_interface)) {
			LOGW("Cannot activate scale switcher!");
			return false;
		}
		if(!grab_interface->grab()) {
			LOGW("Cannot activate scale switcher grab!");
			output->deactivate_plugin(grab_interface);
			return false;
		}
		output->render->add_effect(&pre_render, wf::OUTPUT_EFFECT_PRE);
		//~ output->render->set_redraw_always();
		active = arrange(all_workspaces);
		/* arrange() can return false if no views were found
		 * in this case, we do nothing */
		if(!active) deinit_switcher();
		return active;
	}

	/* The reverse of init_switcher */
	void deinit_switcher() {
		if(grab_interface->is_grabbed()) grab_interface->ungrab();
		output->deactivate_plugin(grab_interface);

		output->render->rem_effect(&pre_render);
		//~ output->render->set_redraw_always(false);
		
		/* remove all transforms */
		for(auto& sv : views) {
			sv.view->pop_transformer(scale_transformer);
			sv.view->disconnect_signal("geometry-changed", &view_geom_changed);
		}
		for(auto& view : get_background_views())
			view->pop_transformer(scale_transformer_background);
		
		views.clear();
		active_view = 0;
		mouse_view = 0;
		have_mouse_view = false;
		selected_view = 0;
		/* TODO: do we need to damage the whole screen here?
		 * in case animations did not finish properly yet */
		output->render->damage_whole();
	}

	
	/* Move view animation target to intended position */
	void move(ScaleView& sv, const wf::geometry_t& box)
	{
		sv.requested_box = box;
		set_view_transforms(sv);
	}
	
	void set_view_transforms(ScaleView& sv) {
		auto box = sv.requested_box;
		const auto& view_box = sv.view->get_bounding_box(scale_transformer);
		if(view_box.width == 0 || view_box.height == 0) {
			sv.exclude_render = true;
			return;
		}
		else sv.exclude_render = false;
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
		sv.box = box;
	}

	
	// returns a list of mapped views
	std::vector<wayfire_view> get_workspace_views(wf::point_t ws) const
	{
		auto all_views = output->workspace->get_views_on_workspace(ws,
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
	bool arrange(bool all_workspaces = false)
	{
		// clear views in case that deinit() hasn't been run
		views.clear();
		
		background_dim.set(1, background_dim_factor);
		background_dim_duration.start();
		
		active_view = 0;
		auto active_view_ptr = output->get_active_view();
		if(all_workspaces) {
			wf::dimensions_t dim = output->workspace->get_workspace_grid_size();
			for(int x = 0; x < dim.width; x++)
				for(int y = 0; y < dim.height; y++) {
					auto ws_views = get_workspace_views({x, y});
					for(auto v : ws_views) {
						if(v == active_view_ptr) active_view = views.size();
						views.push_back(create_scale_view(v));
					}
				}
		}
		else {
			auto ws_views = get_workspace_views(
				output->workspace->get_current_workspace());
			for(auto v : ws_views) {
				if(v == active_view_ptr) active_view = views.size();
				views.push_back(create_scale_view(v));
			}
		}
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
		}

		background_dim.restart_with_end(1);
		background_dim_duration.start();
		duration.start();
		active = false;
	}

	std::vector<wayfire_view> get_background_views() const
	{
		return output->workspace->get_views_on_workspace(
			output->workspace->get_current_workspace(), wf::BELOW_LAYERS, false);
	}

	/*
	std::vector<wayfire_view> get_overlay_views() const
	{
		return output->workspace->get_views_on_workspace(
			output->workspace->get_current_workspace(), wf::ABOVE_LAYERS, false);
	}
	*/
	
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

	ScaleView create_scale_view(wayfire_view view) {
		/* we add a view transform if there isn't any */
		if (!view->get_transformer(scale_transformer))
		{
			view->add_transformer(std::make_unique<wf::view_2D> (view),
				scale_transformer);
		}

		ScaleView sw{duration};
		sw.view = view;
		return sw;
	}

	void update_view_transform(ScaleView& sv)
	{
		const auto& view_box = sv.view->get_bounding_box();
		/* exclude setting transforms on views with zero size */
		if(sv.exclude_render) {
			if(view_box.width > 0 && view_box.height > 0) 
				set_view_transforms(sv);
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

		//~ if(!duration.running()) sv.to_end();
	}
	
	void update_transforms() {
		dim_background(background_dim);
		for(auto& sv : views) update_view_transform(sv);
	}


	/**
	 * Get the relative area of a view visible on a given workspace
	 */
	double get_view_workspace_rel_area(wayfire_view view, wf::point_t ws) {
		wf::geometry_t ws_rel = output->render->get_ws_box(ws);
		wlr_box bbox = view->get_bounding_box(scale_transformer);
		auto intersection = wf::geometry_intersection(bbox, ws_rel);
		double area = 1.0 * intersection.width * intersection.height;
		area /= 1.0 * bbox.width * bbox.height;
		return area;
	}
	
	/**
	 * Get the main workspace of a view (i.e. the workspace with the
	 * largest area).
	 * 
	 * Adapted from src/output/workspace-impl.cpp
	 */
	wf::point_t get_view_main_workspace(wayfire_view view) {
		wf::point_t ret = {0, 0};
		double ret_area = 0.0;
		auto dim = output->workspace->get_workspace_grid_size();
		
		for (int horizontal = 0; horizontal < dim.width; horizontal++) {
			for (int vertical = 0; vertical < dim.height; vertical++) {
				wf::point_t ws = {horizontal, vertical};
				double area = get_view_workspace_rel_area(view, ws);
				if(area > ret_area) {
					ret_area = area;
					ret = ws;
				}
			}
		}
		return ret;
	}


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
