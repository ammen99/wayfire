#ifndef PARTICLE_H_
#define PARTICLE_H_
#include <core.hpp>
#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>

#define NUM_PARTICLES maxParticles
#define WORKGROUP_SIZE 512
#define WORKGROUP_COUNT ((maxParticles + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE)

glm::vec4 operator * (glm::vec4 v, float num);
glm::vec4 operator / (glm::vec4 v, float num);

/* base class for particles */
/* Config for ParticleSystem */

/* base class for a Particle System
 * system consists of maxParticle particles,
 * initial number is startParticles,
 * each iteration partSpawn particles are spawned */

template<class T>
T *get_shader_storage_buffer(GLuint bufID, size_t arrSize);

class wf_particle_system
{
    protected:

    struct particle_t
    {
        float x, y;
        float dx, dy;
        float r, g, b, a;
        int life;
    };
    /* we spawn particles at each 20th iteration */
    size_t currentIteration = 0;

    size_t maxParticles;
    size_t partSpawn;
    size_t particleLife;
    size_t respawnInterval;

    float particleSize;

    GLint renderProg,
          computeProg;
    GLuint vao;
    GLuint base_mesh;

    size_t particleBufSz, lifeBufSz;
    GLuint particleSSbo, lifeInfoSSbo;

    float vertices[12] = {
        -1.f, -1.f,
         1.f, -1.f,
         0.f,  std::sqrt(2.0)
    };

    using particle_tIniter = std::function<void(particle_t&)>;

    bool spawnNew = true;

    PFNGLMEMORYBARRIERPROC memoryBarrierProc = 0;
    PFNGLDISPATCHCOMPUTEPROC dispatchComputeProc = 0;

    /* creates program, VAO, VBO ... */
    virtual void init_gles_part();

    virtual void load_rendering_program();
    virtual void load_compute_program();
    virtual void load_gles_programs();

    virtual void create_buffers();

    /* to change initial particle spawning,
     * override defaultparticle_tIniter */
    virtual void default_particle_initer(particle_t &p);
    virtual void thread_worker_init_particles(particle_t *buff,
            size_t start, size_t end);
    virtual void init_particle_buffer();
    virtual void init_life_info_buffer();

    virtual void gen_base_mesh();
    virtual void upload_base_mesh();
    wf_particle_system();

    public:
    wf_particle_system(float particleSize,
            size_t _maxParticles = 5000,
            size_t _numberSpawned = 200,
            size_t _particleLife = 50,
            size_t _respawnInterval = 25);

    virtual ~wf_particle_system();

    /* simulate movement */
    virtual void simulate();

    /* set particle color */

    virtual void set_particle_color(glm::vec4 scol, glm::vec4 ecol);

    /* render to screen */
    virtual void render();

    /* pause/resume spawning */
    virtual void pause();
    virtual void resume();
};

#endif
