#include <opengl.hpp>
#include <cmath>
#include "fire.hpp"
#include <signal_definitions.hpp>
#include <chrono>
#include <img.hpp>

#define MAX_PARTICLES (512)
#define MIN_PARTICLE_SIZE 0.09
#define MAX_PARTICLE_SIZE 0.12

bool run = true;

#define avg(x,y) (((x) + (y))/2.0)
#define clamp(t,x,y) t=(t > (y) ? (y) : (t < (x) ? (x) : t))

unsigned char data[256 * 256];
GLuint rand_tex;
bool data_filled = false;

class fire_particle_system : public wf_particle_system
{
    float _cx, _cy;
    float _w, _h;

    float global_dx, global_dy;

    float gravity;

    int effect_cycles;

    public:

    void load_compute_program()
    {
        std::string shaderSrcPath = INSTALL_PREFIX"/share/wayfire/animate/shaders";

        computeProg = GL_CALL(glCreateProgram());
        GLuint css =
                OpenGL::load_shader(std::string(shaderSrcPath)
                    .append("/fire_compute.glsl").c_str(),
                    GL_COMPUTE_SHADER);

        GL_CALL(glAttachShader(computeProg, css));
        GL_CALL(glLinkProgram(computeProg));
        GL_CALL(glUseProgram(computeProg));

        GL_CALL(glUniform1i(1, particleLife));
        GL_CALL(glUniform1f(5, 2 * _w));
        GL_CALL(glUniform1f(6, 2 * _h));

        if (!data_filled)
        {
            std::srand(time(0));
            for (int i = 0; i < 256 * 256; i++)
            {
                data[i] = std::rand() % 256;
            }

            GL_CALL(glGenTextures(1, &rand_tex));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, rand_tex));
            GL_CALL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
            GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
            GL_CALL(glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0));
            GL_CALL(glPixelStorei(GL_UNPACK_SKIP_ROWS, 0));

            GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 256, 256, 0, GL_RED, GL_UNSIGNED_BYTE, data));

            data_filled = true;
        }
    }

    void gen_base_mesh()
    {
        wf_particle_system::gen_base_mesh();

        global_dx = global_dy = 0;
        add_offset(_cx - _w, _cy - _h);
    }

    void default_particle_initer(particle_t &p)
    {
        p.life = 0;

        p.dy = 2. * _h * float(std::rand() % 50 + 951) / (950. * effect_cycles);
        p.dx = 0;

        p.x = (float(std::rand() % 1001) / 1000.0) * _w * 2.;
        p.y = (float(std::rand() % 1001) / 1000.0) * _h * 0.02;
    }

    fire_particle_system(float cx, float cy, float w, float h, int numParticles,
            int maxLife, int effect_cycles)
        : _cx(cx), _cy(cy), _w(w), _h(h)
    {
        this->effect_cycles = effect_cycles;

        particleSize = (1. - w) * MIN_PARTICLE_SIZE + w * MAX_PARTICLE_SIZE;

        gravity = -_h * 1 / (maxLife * maxLife / 2) / 3;

        maxParticles    = numParticles;
        partSpawn       = numParticles;
        particleLife    = maxLife;
        respawnInterval = 1;

        init_gles_part();
        set_particle_color(glm::vec4(0.4, 0.17, 0.05, 0.3 + w * 0.1), glm::vec4(0.4, 0.17, 0.05, 0.3));

        GL_CALL(glUseProgram(renderProg));
        GL_CALL(glUniform1f(4, particleSize * 0.8));
        GL_CALL(glUseProgram(0));
    }

    int check()
    {
        /* after first simulation, don't spawn at all */
        pause();

        return currentIteration;
    }

    void simulate()
   {
        GL_CALL(glUseProgram(computeProg));
        GL_CALL(glUniform1f(7, gravity));
        GL_CALL(glUniform1f(8, 0.5 * currentIteration / effect_cycles));

        GL_CALL(glBindTexture(GL_TEXTURE_2D, rand_tex));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));

        wf_particle_system::simulate();

        if ((int)currentIteration >= effect_cycles / 2)
        {
            gravity *= -1;
        }
    }

    void add_offset(float dx, float dy)
    {
        global_dx += dx;
        global_dy += dy;

        float data[] = {global_dx, global_dy};

        GL_CALL(glUseProgram(renderProg));
        GL_CALL(glUniform2fv(3, 1, data));
    }
};

void wf_fire_effect::init(wayfire_view win, int fr_cnt, bool burnout)
{

    this->burnout = burnout;
    this->w = win;
    effect_cycles = fr_cnt;

    auto x = w->geometry.x,
         y = w->geometry.y,
         wi = w->geometry.width,
         he = w->geometry.height;

    float sw = w->output->render->ctx->device_width;
    float sh = w->output->render->ctx->device_height;

    float w2 = float(sw) / 2.,
          h2 = float(sh) / 2.;

    float tlx = (x - w2) / w2,
          tly = (h2 - y) / h2;

    float brx = tlx + wi / w2,
          bry = tly - he / h2;

    /* "short" surfaces need less time to burn,
     * however try not to make the effect too short */
    float percent = 1.0 * he / sh;
    percent = std::pow(percent, 1.0 / 5.0);
    fr_cnt *= percent;
    effect_cycles = fr_cnt;

    ps = new fire_particle_system(avg(tlx, brx), avg(tly, bry),
            wi / sw, he / sh, MAX_PARTICLES, fr_cnt * 3, fr_cnt);

    win->transform.color = glm::vec4(1, 1, 1, 0);
    progress = 0;

    last_geometry = win->geometry;
}

bool wf_fire_effect::step()
{
    if (w->geometry.x != last_geometry.x || w->geometry.y != last_geometry.y)
    {
        int dx = w->geometry.x - last_geometry.x;
        int dy = w->geometry.y - last_geometry.y;

        GetTuple(sw, sh, w->output->get_screen_size());

        float fdx = 2. * float(dx) / float(sw);
        float fdy = 2. * float(dy) / float(sh);

        ps->add_offset(fdx, -fdy);

        last_geometry = w->geometry;
    }

    ps->simulate();
    adjust_alpha();

    if(w->is_mapped)
    {
        pixman_region32_t visible_region;

        float a = 1.0 * progress / effect_cycles;
        if (burnout)
        {
            pixman_region32_init_rect(&visible_region,
                                      w->geometry.x, w->geometry.y,
                                      w->geometry.width, w->geometry.height * (1 - a));
        } else
        {
            pixman_region32_init_rect(&visible_region,
                                      w->geometry.x, w->geometry.y + w->geometry.height * (1 - a),
                                      w->geometry.width, w->geometry.height * a);
        }

        OpenGL::use_default_program();
        w->simple_render(0, &visible_region);

        w->transform.color[3] = 0.0f;
        ps->render();
    }

    return ps->check() <= effect_cycles;
}

wf_fire_effect::~wf_fire_effect()
{
    w->transform.color[3] = 1;
    delete ps;
}

void wf_fire_effect::adjust_alpha()
{
    if (burnout)
    {
        w->transform.color[3] = GetProgress(1.0f, 0.5f, progress, effect_cycles);
    } else
    {
        w->transform.color[3] = GetProgress(0.5f, 1.0f, progress, effect_cycles);
    }
    ++progress;
}
