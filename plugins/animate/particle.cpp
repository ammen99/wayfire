#include "particle.hpp"
#include <opengl.hpp>

#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>
#include <EGL/egl.h>

#include <thread>

glm::vec4 operator * (glm::vec4 v, float x)
{
    v[0] *= x;
    v[1] *= x;
    v[2] *= x;
    v[3] *= x;
    return v;
}

glm::vec4 operator / (glm::vec4 v, float x)
{
    v[0] /= x;
    v[1] /= x;
    v[2] /= x;
    v[3] /= x;
    return v;
}

template<class T>
T *get_shader_storage_buffer(GLuint bufID, size_t arrSize)
{
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufID);
    glBufferData(GL_SHADER_STORAGE_BUFFER, arrSize, NULL, GL_STATIC_DRAW);

    GLint mask =  GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT;

    return (T*) glMapBufferRange(GL_SHADER_STORAGE_BUFFER,
                                 0, arrSize, mask);
}

/* Implementation of ParticleSystem */

void wf_particle_system::load_rendering_program()
{
    renderProg = glCreateProgram();
    GLuint vss, fss;
    std::string shaderSrcPath = INSTALL_PREFIX"/share/wayfire/animate/shaders";

    vss = OpenGL::load_shader(std::string(shaderSrcPath)
            .append("/vertex.glsl").c_str(), GL_VERTEX_SHADER);

    fss = OpenGL::load_shader(std::string(shaderSrcPath)
            .append("/frag.glsl").c_str(), GL_FRAGMENT_SHADER);

    GL_CALL(glAttachShader(renderProg, vss));
    GL_CALL(glAttachShader (renderProg, fss));

    GL_CALL(glLinkProgram (renderProg));
    GL_CALL(glUseProgram(renderProg));

    GL_CALL(glUniform1f(4, std::sqrt(2.0) * particleSize));
}

void wf_particle_system::load_compute_program()
{
    std::string shaderSrcPath = INSTALL_PREFIX"/share/wayfire/animate/shaders";

    computeProg = GL_CALL(glCreateProgram());
    GLuint css =
        OpenGL::load_shader(std::string(shaderSrcPath)
                .append("/compute.glsl").c_str(),
                GL_COMPUTE_SHADER);

    GL_CALL(glAttachShader(computeProg, css));
    GL_CALL(glLinkProgram(computeProg));
    GL_CALL(glUseProgram(computeProg));

    GL_CALL(glUniform1i(1, particleLife));
}

void wf_particle_system::load_gles_programs()
{
    load_rendering_program();
    load_compute_program();
}

void wf_particle_system::create_buffers()
{
    GL_CALL(glGenBuffers(1, &base_mesh));
    GL_CALL(glGenBuffers(1, &particleSSbo));
    GL_CALL(glGenBuffers(1, &lifeInfoSSbo));
}

void wf_particle_system::default_particle_initer(particle_t &p)
{
    p.life = particleLife + 1;

    p.x = p.y = -2;
    p.dx = float(std::rand() % 1001 - 500) / (500 * particleLife);
    p.dy = float(std::rand() % 1001 - 500) / (500 * particleLife);

    p.r = p.g = p.b = p.a = 0;
}

void wf_particle_system::thread_worker_init_particles(particle_t *p,
                                                      size_t start, size_t end)
{
    for(size_t i = start; i < end; ++i)
        default_particle_initer(p[i]);

}

void wf_particle_system::init_particle_buffer()
{

    particleBufSz = maxParticles * sizeof(particle_t);

    particle_t *p = get_shader_storage_buffer<particle_t>(particleSSbo,
                                                          particleBufSz);

    using namespace std::placeholders;
    auto threadFunction =
        std::bind(std::mem_fn(&wf_particle_system::thread_worker_init_particles),
                  this, _1, _2, _3);

    size_t sz = std::thread::hardware_concurrency();

    int interval = maxParticles / sz;
    if(maxParticles % sz != 0)
        ++interval;

    std::vector<std::thread> threads;
    threads.resize(sz);

    for(size_t i = 0; i < sz; ++i)
    {
        auto start = i * interval;
        auto end   = std::min((i + 1) * interval, maxParticles);
        threads[i] = std::thread(threadFunction, p, start, end);
    }

    for(size_t i = 0; i < sz; ++i)
        threads[i].join();

    GL_CALL(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
}

void wf_particle_system::init_life_info_buffer()
{
    lifeBufSz = sizeof(int) * maxParticles;
    int *lives = get_shader_storage_buffer<int>(lifeInfoSSbo, lifeBufSz);

    for(size_t i = 0; i < maxParticles; ++i)
        lives[i] = 0;

    GL_CALL(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
    GL_CALL(memoryBarrierProc(GL_ALL_BARRIER_BITS));
}

void wf_particle_system::gen_base_mesh()
{
    /* scale base mesh */
    for(size_t i = 0; i < sizeof(vertices) / sizeof(float); i++)
        vertices[i] *= particleSize;
}

void wf_particle_system::upload_base_mesh()
{
    GL_CALL(glUseProgram(renderProg));

    GL_CALL(glGenVertexArrays(1, &vao));
    GL_CALL(glBindVertexArray(vao));

    /* upload static base mesh */
    GL_CALL(glEnableVertexAttribArray(0));
    GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, base_mesh));
    GL_CALL(glBufferData(GL_ARRAY_BUFFER, sizeof(vertices),
                         vertices, GL_STATIC_DRAW));
    GL_CALL(glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, 0));
    GL_CALL(glDisableVertexAttribArray(0));

    GL_CALL(glBindVertexArray(0));
    GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, 0));
    GL_CALL(glUseProgram(0));
}

void wf_particle_system::init_gles_part()
{
    memoryBarrierProc =
        (PFNGLMEMORYBARRIERPROC) eglGetProcAddress("glMemoryBarrier");
    dispatchComputeProc =
        (PFNGLDISPATCHCOMPUTEPROC) eglGetProcAddress("glDispatchCompute");

    if (!memoryBarrierProc || !dispatchComputeProc)
    {
        errio << "missing compute shader functionality, can't use fire effect!" << std::endl;
        return;
    }

    load_gles_programs();
    create_buffers();

    init_particle_buffer();
    init_life_info_buffer();
    gen_base_mesh();
    upload_base_mesh();
}

void wf_particle_system::set_particle_color(glm::vec4 scol,
                                            glm::vec4 ecol)
{

    GL_CALL(glUseProgram(computeProg));
    GL_CALL(glUniform4fv(2, 1, &scol[0]));
    GL_CALL(glUniform4fv(3, 1, &ecol[0]));

    auto tmp = (ecol - scol) / float(particleLife);
    GL_CALL(glUniform4fv(4, 1, &tmp[0]));
}

wf_particle_system::wf_particle_system() {}
wf_particle_system::wf_particle_system(float size,  size_t _maxp,
                                       size_t _pspawn, size_t _plife,
                                       size_t _respInterval)
{
    particleSize = size;

    maxParticles    = _maxp;
    partSpawn       = _pspawn;
    particleLife    = _plife;
    respawnInterval = _respInterval;

    init_gles_part();
    set_particle_color(glm::vec4(0, 0, 1, 1), glm::vec4(1, 0, 0, 1));
}

wf_particle_system::~wf_particle_system()
{

    GL_CALL(glDeleteBuffers(1, &particleSSbo));
    GL_CALL(glDeleteBuffers(1, &lifeInfoSSbo));
    GL_CALL(glDeleteBuffers(1, &base_mesh));
    GL_CALL(glDeleteVertexArrays(1, &vao));

    GL_CALL(glDeleteProgram(renderProg));
    GL_CALL(glDeleteProgram(computeProg));
}

void wf_particle_system::pause () {spawnNew = false;}
void wf_particle_system::resume() {spawnNew = true; }

void wf_particle_system::simulate()
{
    GL_CALL(glUseProgram(computeProg));

    if(currentIteration++ % respawnInterval == 0 && spawnNew)
    {
        GL_CALL(glUseProgram(computeProg));

        GL_CALL(glBindBuffer(GL_SHADER_STORAGE_BUFFER, lifeInfoSSbo));
        auto lives =
            (int*) GL_CALL(glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0,
                                             sizeof(GLint) * maxParticles,
                                             GL_MAP_WRITE_BIT | GL_MAP_READ_BIT));

        size_t sp_num = partSpawn, i = 0;

        while(i < maxParticles && sp_num > 0)
        {
            if(lives[i] == 0)
            {
                lives[i] = 1;
                --sp_num;
            }

            ++i;
        }

        GL_CALL(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
        GL_CALL(glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0));
    }

    GL_CALL(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, particleSSbo));
    GL_CALL(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, lifeInfoSSbo));

    GL_CALL(dispatchComputeProc(WORKGROUP_COUNT, 1, 1));
    GL_CALL(memoryBarrierProc(GL_ALL_BARRIER_BITS));
    GL_CALL(glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0));
    GL_CALL(glUseProgram(0));
}


/* TODO: use glDrawElementsInstanced instead of glDrawArraysInstanced */
void wf_particle_system::render()
{
    GL_CALL(glUseProgram(renderProg));
    GL_CALL(glEnable(GL_BLEND));
    GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE));

    GL_CALL(glBindVertexArray(vao));

    /* prepare vertex attribs */
    GL_CALL(glEnableVertexAttribArray(0));
    GL_CALL(glBindBuffer (GL_ARRAY_BUFFER, base_mesh));
    GL_CALL(glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, 0));

    GL_CALL(glEnableVertexAttribArray(1));
    GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, particleSSbo));
    GL_CALL(glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE,
                                   sizeof(particle_t), 0));

    GL_CALL(glEnableVertexAttribArray(2));
    GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, particleSSbo));

    GL_CALL(glVertexAttribPointer (2, 4, GL_FLOAT, GL_FALSE,
                                   sizeof(particle_t),
                                   (void*) (4 * sizeof(float))));

    GL_CALL(glVertexAttribDivisor(0, 0));
    GL_CALL(glVertexAttribDivisor(1, 1));
    GL_CALL(glVertexAttribDivisor(2, 1));

    /* draw particles */
    GL_CALL(glDrawArraysInstanced(GL_TRIANGLES, 0, 6,
                                  maxParticles));

    GL_CALL(glDisableVertexAttribArray(0));
    GL_CALL(glDisableVertexAttribArray(1));
    GL_CALL(glDisableVertexAttribArray(2));

    GL_CALL(glBindVertexArray(0));
    GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, 0));
    GL_CALL(glUseProgram(0));
}

