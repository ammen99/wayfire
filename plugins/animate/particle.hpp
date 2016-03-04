#ifndef PARTICLE_H_
#define PARTICLE_H_
#include <core.hpp>

glm::vec4 operator * (glm::vec4 v, float num);
glm::vec4 operator / (glm::vec4 v, float num);

/* base class for particles */
/* Config for ParticleSystem */

/* base class for a Particle System
 * system consists of maxParticle particles,
 * initial number is startParticles,
 * each iteration partSpawn particles are spawned */

template<class T>
T *getShaderStorageBuffer(GLuint bufID, size_t arrSize);

class ParticleSystem {
    protected:

    struct Particle {
        float life;
        float x, y;
        float dx, dy;
        float r, g, b, a;
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

    float uv[24] = {
        -1.f, -1.f, 0.0f, 0.0f,
         1.f, -1.f, 1.0f, 0.0f,
         1.f,  1.f, 1.0f, 1.0f,
         1.f,  1.f, 1.0f, 1.0f,
        -1.f,  1.f, 0.0f, 1.0f,
        -1.f, -1.f, 0.0f, 0.0f,
    };

    float vertices[12] = {
        -1.f, -1.f,
         1.f, -1.f,
         1.f,  1.f,
         1.f,  1.f,
        -1.f,  1.f,
        -1.f, -1.f,
    };

    using ParticleIniter = std::function<void(Particle&)>;

    bool spawnNew = true;

    /* creates program, VAO, VBO ... */
    virtual void initGLPart();

    virtual void loadRenderingProgram();
    virtual void loadComputeProgram();
    virtual void loadGLPrograms();

    virtual void createBuffers();

    /* to change initial particle spawning,
     * override defaultParticleIniter */
    virtual void defaultParticleIniter(Particle &p);
    virtual void threadWorker_InitParticles(Particle *buff,
            size_t start, size_t end);
    virtual void initParticleBuffer();
    virtual void initLifeInfoBuffer();

    virtual void genBaseMesh();
    virtual void uploadBaseMesh();
    ParticleSystem();

    public:
    ParticleSystem(float particleSize,
            size_t _maxParticles = 5000,
            size_t _numberSpawned = 200,
            size_t _particleLife = 50,
            size_t _respawnInterval = 25);

    virtual ~ParticleSystem();

    /* simulate movement */
    virtual void simulate();

    /* set particle color */

    virtual void setParticleColor(glm::vec4 scol, glm::vec4 ecol);

    /* render to screen */
    virtual void render();

    /* pause/resume spawning */
    virtual void pause();
    virtual void resume();
};

#endif
