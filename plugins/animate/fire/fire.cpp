#include "fire.hpp"
#include "particle.hpp"

#include <thread>
#include <output.hpp>
#include <core.hpp>
#include <glm/gtc/matrix_transform.hpp>

wf_option FireAnimation::fire_particles;
wf_option FireAnimation::fire_particle_size;

// generate a random float between s and e
static float random(float s, float e)
{
    double r = 1.0 * (std::rand() % RAND_MAX) / (RAND_MAX - 1);
    return (s * r + (1 - r) * e);
}

static int particle_count_for_width(int width)
{
    int particles = FireAnimation::fire_particles->as_cached_int();

    return particles * std::min(width / 400.0, 3.5);
}

class FireTransformer : public wf_view_transformer_t
{
    wf_geometry last_boundingbox;
    wf_duration duration;

    public:
    ParticleSystem ps;

    FireTransformer(wayfire_view view) :
        ps(FireAnimation::fire_particles->as_cached_int(),
           [=] (Particle& p) {init_particle(p); })
    {
        last_boundingbox = view->get_bounding_box();
        ps.resize(particle_count_for_width(last_boundingbox.width));
    }

    ~FireTransformer() { }

    virtual wf_point local_to_transformed_point(wf_geometry view, wf_point point) { return point; }
    virtual wf_point transformed_to_local_point(wf_geometry view, wf_point point) { return point; }

    static constexpr int left_border = 50;
    static constexpr int right_border = 50;
    static constexpr int top_border = 100;
    static constexpr int bottom_border = 50;

    uint32_t get_z_order() { return WF_TRANSFORMER_HIGHLEVEL + 1; }

    virtual wlr_box get_bounding_box(wf_geometry view, wlr_box region)
    {
        last_boundingbox = view;
        ps.resize(particle_count_for_width(last_boundingbox.width));

         // TODO
        //
        view.x -= left_border;
        view.y -= top_border;
        view.width += left_border + right_border;
        view.height += top_border + bottom_border;
        return view;
    }

    float progress_line;
    void set_progress_line(float line)
    {
        progress_line = line;
    }

    void init_particle(Particle& p)
    {
        p.life = 1;
        p.fade = random(0.1, 0.6);

        p.color = {random(0.4, 1), random(0.08, 0.2), random(0.008, 0.018), 1};

        p.pos = {random(0, last_boundingbox.width),
            random(last_boundingbox.height * progress_line - 10,
                   last_boundingbox.height * progress_line + 10)};
        p.start_pos = p.pos;

        p.speed = {random(-10, 10), random(-25, 5)};
        p.g = {-1, -3};

        double size = FireAnimation::fire_particle_size->as_double();
        p.base_radius = p.radius = random(size * 0.8, size * 1.2);
    }

    virtual void render_box(uint32_t src_tex, wlr_box src_box,
        wlr_box scissor_box, const wf_framebuffer& target_fb)
    {
        OpenGL::render_begin(target_fb);
        target_fb.scissor(scissor_box);

        // render view
        float x = src_box.x, y = src_box.y, w = src_box.width, h = src_box.height;
        gl_geometry src_geometry = {x, y, x + w, y + h * progress_line};

        gl_geometry tex_geometry = {
            0, 1,
            1, 1 - progress_line,
        };

        OpenGL::render_transformed_texture(src_tex, src_geometry, tex_geometry,
                                           target_fb.get_orthographic_projection(),
                                           glm::vec4(1.0), TEXTURE_USE_TEX_GEOMETRY);

        auto translate = glm::translate(glm::mat4(1.0),
                                        {src_box.x, src_box.y, 0});

        ps.render(target_fb.get_orthographic_projection() * translate);
        OpenGL::render_end();
    }
};

static float fire_duration_mod_for_height(int height)
{
    return std::min(height / 400.0, 3.0);
}

void FireAnimation::init(wayfire_view view, wf_option dur, wf_animation_type type)
{

    this->view = view;

    int msec = dur->as_int() * fire_duration_mod_for_height(
        view->get_bounding_box().height);
    this->duration = wf_duration(new_static_option(std::to_string(msec)),
                                 wf_animation::linear);

    if (type & HIDING_ANIMATION) {
        duration.start(1, 0);
    } else {
        duration.start(0, 1);
    }

    name = "animation-fire-" + std::to_string(type);
    auto tr = std::make_unique<FireTransformer>(view);
    transformer = decltype(transformer) (tr.get());

    view->add_transformer(std::move(tr), name);
}

bool FireAnimation::step()
{
    transformer->set_progress_line(duration.progress());
    if (duration.running())
        transformer->ps.spawn(transformer->ps.size() / 10);

    transformer->ps.update();
    return duration.running() || transformer->ps.statistic();
}

FireAnimation::~FireAnimation()
{
    view->pop_transformer(name);
}
