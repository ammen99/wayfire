#include <plugin.hpp>
#include <signal-definitions.hpp>
#include <core.hpp>
#include <view-transform.hpp>
#include <workspace-manager.hpp>
#include <render-manager.hpp>

extern "C"
{
#include "wobbly.h"
}

#include "wobbly-signal.hpp"

namespace wobbly_graphics
{
    namespace
    {
        const char* vertex_source = R"(
#version 100
attribute mediump vec2 position;
attribute mediump vec2 uvPosition;
varying highp vec2 uvpos;
uniform mat4 MVP;

void main() {
    gl_Position = MVP * vec4(position.xy, 0.0, 1.0);
    uvpos = uvPosition;
}
)";

        const char *frag_source = R"(
#version 100
varying highp vec2 uvpos;
uniform sampler2D smp;

void main()
{
    gl_FragColor = texture2D(smp, uvpos);
}
)";
    }

    GLuint program, uvID, posID, mvpID;

    int times_loaded = 0;

    void load_program()
    {
        if (times_loaded++ > 0)
            return;

        OpenGL::render_begin();
        program = OpenGL::create_program_from_source(vertex_source, frag_source);
        uvID  = GL_CALL(glGetAttribLocation(program, "uvPosition"));
        posID = GL_CALL(glGetAttribLocation(program, "position"));
        mvpID = GL_CALL(glGetUniformLocation(program, "MVP"));
        OpenGL::render_end();
    }

    void destroy_program()
    {
        if (--times_loaded == 0)
        {
            OpenGL::render_begin();
            GL_CALL(glDeleteProgram(program));
            OpenGL::render_end();
        }
    }

    /* Requires bound opengl context */
    void render_triangles(GLuint tex, glm::mat4 mat, float *pos, float *uv, int cnt)
    {
        GL_CALL(glUseProgram(program));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, tex));
        GL_CALL(glActiveTexture(GL_TEXTURE0));

        GL_CALL(glVertexAttribPointer(posID, 2, GL_FLOAT, GL_FALSE, 0, pos));
        GL_CALL(glEnableVertexAttribArray(posID));

        GL_CALL(glVertexAttribPointer(uvID, 2, GL_FLOAT, GL_FALSE, 0, uv));
        GL_CALL(glEnableVertexAttribArray(uvID));

        GL_CALL(glUniformMatrix4fv(mvpID, 1, GL_FALSE, &mat[0][0]));
        GL_CALL(glEnable(GL_BLEND));
        GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

        GL_CALL(glDrawArrays (GL_TRIANGLES, 0, 3 * cnt));
        GL_CALL(glDisable(GL_BLEND));

        GL_CALL(glDisableVertexAttribArray(uvID));
        GL_CALL(glDisableVertexAttribArray(posID));
    }
};

namespace wobbly_settings
{
    wf_option friction, spring_k, resolution;

    void init(wayfire_config *config)
    {
        auto section = config->get_section("wobbly");
        friction = section->get_option("friction", "3");
        spring_k = section->get_option("spring_k", "8");
        resolution = section->get_option("grid_resolution", "6");
    };
};

extern "C"
{
    double wobbly_settings_get_friction()
    {
        return clamp(wobbly_settings::friction->as_cached_double(),
            MINIMAL_FRICTION, MAXIMAL_FRICTION);
    }

    double wobbly_settings_get_spring_k()
    {
        return clamp(wobbly_settings::spring_k->as_cached_double(),
            MINIMAL_SPRING_K, MAXIMAL_SPRING_K);
    }
}

class wf_wobbly : public wf_view_transformer_t
{
    wayfire_view view;
    wf::effect_hook_t pre_hook;
    wf::signal_callback_t view_removed, view_geometry_changed,
        view_output_changed;
    const wf::plugin_grab_interface_uptr& iface;

    std::unique_ptr<wobbly_surface> model;

    bool has_active_grab = false;
    int grab_x = 0, grab_y = 0;

    wf_geometry snapped_geometry;
    uint32_t last_frame;

    public:
    wf_wobbly(wayfire_view view, const wf::plugin_grab_interface_uptr& _iface)
        : iface(_iface)
    {
        this->view = view;
        model = std::make_unique<wobbly_surface> ();
        auto g = view->get_bounding_box();

        model->x = g.x;
        model->y = g.y;
        model->width = g.width;
        model->height = g.height;

        model->grabbed = 0;
        model->synced = 1;

        model->x_cells = wobbly_settings::resolution->as_cached_int();
        model->y_cells = wobbly_settings::resolution->as_cached_int();

        model->v = NULL;
        model->uv = NULL;

        last_frame = get_current_time();
        wobbly_init(model.get());

        pre_hook = [=] () {
            update_model();
        };
        view->get_output()->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);

        view_removed = [=] (wf::signal_data_t *data) {
            destroy_self();
        };

        view_geometry_changed = [=] (wf::signal_data_t *data) {
            auto sig = static_cast<view_geometry_changed_signal*> (data);
            update_view_geometry(sig->old_geometry);
        };

        view_output_changed = [=] (wf::signal_data_t *data) {
            auto sig = static_cast<_output_signal*> (data);

            if (!view->get_output())
                return destroy_self();

            /* Wobbly is active only when there's already been an output */
            assert(sig->output);

            sig->output->render->rem_effect(&pre_hook);
            view->get_output()->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        };

        view->connect_signal("unmap", &view_removed);
        view->connect_signal("set-output", &view_output_changed);
        view->connect_signal("geometry-changed", &view_geometry_changed);
    }

    uint32_t get_z_order() { return WF_TRANSFORMER_HIGHLEVEL; }

    virtual wlr_box get_bounding_box(wf_geometry, wf_geometry)
    {
        auto box = wobbly_boundingbox(model.get());

        wlr_box result;
        result.x = box.tlx;
        result.y = box.tly;
        result.width = std::ceil(box.brx - box.tlx);
        result.height = std::ceil(box.bry - box.tly);

        return result;
    }

    virtual wf_point local_to_transformed_point(wf_geometry view, wf_point point) { return {0, 0}; }
    virtual wf_point transformed_to_local_point(wf_geometry view, wf_point point)
    {
        return point;
    }

    void update_model()
    {
        view->damage();

        auto bbox = view->get_bounding_box("wobbly");
        if (snapped_geometry.width <= 0)
            resize(bbox.width, bbox.height);

        auto now = get_current_time();
        wobbly_prepare_paint(model.get(), now - last_frame);
        last_frame = now;

        wobbly_add_geometry(model.get());
        wobbly_done_paint(model.get());

        view->damage();

        if (snapped_geometry.width <= 0 && !has_active_grab)
        {
            /* We temporarily don't want to receive updates on the view's
             * geometry, because we usually adjust the model based on the
             * view's movements. However in this case, we are syncing the
             * view geometry with the model. If we then updated the model
             * based on the view geometry, we'd get a feedback loop */
            view->disconnect_signal("geometry-changed", &this->view_geometry_changed);
            auto wm = view->get_wm_geometry();
            view->move(model->x + wm.x - bbox.x, model->y + wm.y - bbox.y);
            view->connect_signal("geometry-changed", &this->view_geometry_changed);
        }

        if (!has_active_grab && model->synced)
            destroy_self();
    }

    virtual void render_box(uint32_t src_tex, wlr_box src_box,
        wlr_box scissor_box, const wf_framebuffer& target_fb)
    {
        OpenGL::render_begin(target_fb);
        target_fb.scissor(scissor_box);

        float x = src_box.x, y = src_box.y, w = src_box.width, h = src_box.height;

        std::vector<float> vert, uv;
        std::vector<int> idx;

        int per_row = model->x_cells + 1;

        for (int j = 0; j < model->y_cells; j++)
        {
            for (int i = 0; i < model->x_cells; i++)
            {
                idx.push_back(i * per_row + j);
                idx.push_back((i + 1) * per_row + j + 1);
                idx.push_back(i * per_row + j + 1);

                idx.push_back(i * per_row + j);
                idx.push_back((i + 1) * per_row + j);
                idx.push_back((i + 1) * per_row + j + 1);
            }
        }

        if (!model->v || !model->uv)
        {
            for (auto id : idx)
            {
                float tile_w = w / model->x_cells;
                float tile_h = h / model->y_cells;

                int i = id / per_row;
                int j = id % per_row;

                vert.push_back(i * tile_w + x);
                vert.push_back(j * tile_h + y);

                uv.push_back(1.0f * i / model->x_cells);
                uv.push_back(1.0f - 1.0f * j / model->y_cells);
            }
        } else
        {
            for (auto i : idx)
            {
                vert.push_back(model->v[2 * i]);
                vert.push_back(model->v[2 * i + 1]);

                uv.push_back(model->uv[2 * i]);
                uv.push_back(model->uv[2 * i + 1]);
            }
        }

        wobbly_graphics::render_triangles(src_tex,
            target_fb.get_orthographic_projection(),
            vert.data(), uv.data(), model->x_cells * model->y_cells * 2);

        OpenGL::render_end();
    }

    void start_grab(int x, int y)
    {
        grab_x = x;
        grab_y = y;
        has_active_grab = 1;
        wobbly_grab_notify(model.get(), x, y);
        unsnap();
    }

    void move(int x, int y)
    {
        wobbly_move_notify(model.get(), x - grab_x, y - grab_y);
        grab_x = x;
        grab_y = y;
    }

    void resize(int w, int h)
    {
        model->width = w;
        model->height = h;
        wobbly_resize_notify(model.get());
    }

    void end_grab(bool unanchor)
    {
        if (has_active_grab && unanchor)
            wobbly_ungrab_notify(model.get());
        has_active_grab = false;
    }

    void snap(wf_geometry geometry)
    {
        wobbly_force_geometry(model.get(),
            geometry.x, geometry.y, geometry.width, geometry.height);
        snapped_geometry = geometry;
    }

    void unsnap()
    {
        wobbly_unenforce_geometry(model.get());
        snapped_geometry.width = -1;
    }

    void translate(int dx, int dy)
    {
        wobbly_translate(model.get(), dx, dy);
        wobbly_add_geometry(model.get());
    }

    void destroy_self()
    {
        view->pop_transformer("wobbly");
    }

    void update_view_geometry(wf_geometry old_geometry)
    {
        if (has_active_grab)
            return;

        auto wm = view->get_wm_geometry();

        int dx = wm.x - old_geometry.x;
        int dy = wm.y - old_geometry.y;
        translate(dx, dy);
    }

    virtual ~wf_wobbly()
    {
        wobbly_fini(model.get());
        view->get_output()->render->rem_effect(&pre_hook);

        view->disconnect_signal("unmap", &view_removed);
        view->disconnect_signal("set-output", &view_output_changed);
        view->disconnect_signal("geometry-changed", &view_geometry_changed);
    }
};

class wayfire_wobbly : public wf::plugin_interface_t
{
    wf::signal_callback_t wobbly_changed;
    public:
        void init(wayfire_config *config)
        {
            wobbly_settings::init(config);
            grab_interface->capabilities = 0;
            grab_interface->name = "wobbly";

            wobbly_changed = [=] (wf::signal_data_t *data)
            {
                adjust_wobbly(static_cast<wobbly_signal*> (data));
            };

            output->connect_signal("wobbly-event", &wobbly_changed);

            wobbly_graphics::load_program();
        }

        void adjust_wobbly(wobbly_signal *data)
        {
            if (data->view->get_output() != output)
                return;

            if ((data->events & (WOBBLY_EVENT_GRAB | WOBBLY_EVENT_SNAP))
                && data->view->get_transformer("wobbly") == nullptr)
                data->view->add_transformer(std::make_unique<wf_wobbly> (data->view, grab_interface), "wobbly");

            auto wobbly = dynamic_cast<wf_wobbly*> (data->view->get_transformer("wobbly").get());
            if (!wobbly)
                return;

            if (data->events & WOBBLY_EVENT_GRAB)
                wobbly->start_grab(data->grab_x, data->grab_y);

            if (data->events & WOBBLY_EVENT_MOVE)
                wobbly->move(data->geometry.x, data->geometry.y);

            if (data->events & WOBBLY_EVENT_END)
                wobbly->end_grab(data->unanchor);

            if (data->events & WOBBLY_EVENT_SNAP)
                wobbly->snap(data->geometry);

            if (data->events & WOBBLY_EVENT_UNSNAP)
                wobbly->unsnap();

            if (data->events & WOBBLY_EVENT_TRANSLATE)
                wobbly->translate(data->geometry.x, data->geometry.y);
        }

        void fini()
        {
            for (auto& view : output->workspace->get_views_in_layer(wf::ALL_LAYERS))
            {
                auto wobbly = dynamic_cast<wf_wobbly*> (view->get_transformer("wobbly").get());
                if (wobbly)
                    wobbly->destroy_self();
            }

            wobbly_graphics::destroy_program();
            output->disconnect_signal("wobbly-event", &wobbly_changed);
        }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_wobbly);
