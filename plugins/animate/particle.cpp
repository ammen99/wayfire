#include "particle.hpp"
#include <opengl.hpp>
#include <thread>

#define NUM_PARTICLES maxParticles
#define NUM_WORKGROUPS 384
#define WORKGROUP_SIZE (NUM_PARTICLES / NUM_WORKGROUPS)


glm::vec4 operator * (glm::vec4 v, float x) {
    v[0] *= x;
    v[1] *= x;
    v[2] *= x;
    v[3] *= x;
    return v;
}

glm::vec4 operator / (glm::vec4 v, float x) {
    v[0] /= x;
    v[1] /= x;
    v[2] /= x;
    v[3] /= x;
    return v;
}

template<class T>
T *getShaderStorageBuffer(GLuint bufID, size_t arrSize) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufID);
    glBufferData(GL_SHADER_STORAGE_BUFFER, arrSize,
            NULL, GL_STATIC_DRAW);

    GLint mask =  GL_MAP_WRITE_BIT |
        GL_MAP_INVALIDATE_BUFFER_BIT;

    return (T*) glMapBufferRange(GL_SHADER_STORAGE_BUFFER,
            0, arrSize, mask);
}

/* Implementation of ParticleSystem */

void ParticleSystem::loadRenderingProgram() {
    renderProg = glCreateProgram();
    GLuint vss, fss;
    std::string shaderSrcPath = "/usr/local/share/fireman/animate/shaders";

    vss = GLXUtils::loadShader(std::string(shaderSrcPath)
            .append("/vertex.glsl").c_str(), GL_VERTEX_SHADER);

    fss = GLXUtils::loadShader(std::string(shaderSrcPath)
            .append("/frag.glsl").c_str(), GL_FRAGMENT_SHADER);

    glAttachShader (renderProg, vss);
    glAttachShader (renderProg, fss);

    glBindFragDataLocation (renderProg, 0, "outColor");
    glLinkProgram (renderProg);
    glUseProgram(renderProg);
}

void ParticleSystem::loadComputeProgram() {
    std::string shaderSrcPath = "/usr/local/share/fireman/animate/shaders";

    computeProg = glCreateProgram();
    GLuint css =
        GLXUtils::loadShader(std::string(shaderSrcPath)
                .append("/compute.glsl").c_str(),
                GL_COMPUTE_SHADER);

    glAttachShader(computeProg, css);
    glLinkProgram(computeProg);
    glUseProgram(computeProg);

    glUniform1f(1, particleLife);
}

void ParticleSystem::loadGLPrograms() {
    loadRenderingProgram();
    loadComputeProgram();
}

void ParticleSystem::createBuffers() {
    glGenBuffers(1, &base_mesh);
    glGenBuffers(1, &particleSSbo);
    glGenBuffers(1, &lifeInfoSSbo);
}

void ParticleSystem::defaultParticleIniter(Particle &p) {
    p.life = particleLife + 1;

    p.x = p.y = -2;
    p.dx = float(std::rand() % 1001 - 500) / (500 * particleLife);
    p.dy = float(std::rand() % 1001 - 500) / (500 * particleLife);

    p.r = p.g = p.b = p.a = 0;
}

void ParticleSystem::threadWorker_InitParticles(Particle *p,
        size_t start, size_t end) {

    for(size_t i = start; i < end; ++i)
        defaultParticleIniter(p[i]);

}

void ParticleSystem::initParticleBuffer() {

    particleBufSz = maxParticles * sizeof(Particle);

    Particle *p = getShaderStorageBuffer<Particle>(particleSSbo,
            particleBufSz);

    using namespace std::placeholders;
    auto threadFunction =
        std::bind(std::mem_fn(&ParticleSystem::threadWorker_InitParticles),
                this, _1, _2, _3);

    size_t sz = std::thread::hardware_concurrency();

    int interval = maxParticles / sz;
    if(maxParticles % sz != 0)
        ++interval;

    std::vector<std::thread> threads;
    threads.resize(sz);

    for(size_t i = 0; i < sz; ++i) {
        auto start = i * interval;
        auto end   = std::min((i + 1) * interval, maxParticles);
        threads[i] = std::thread(threadFunction, p, start, end);
    }

    for(size_t i = 0; i < sz; ++i)
        threads[i].join();

    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
}

void ParticleSystem::initLifeInfoBuffer() {
    lifeBufSz = sizeof(uint) * maxParticles;
    uint *lives = getShaderStorageBuffer<uint>(lifeInfoSSbo, lifeBufSz);

    for(int i = 0; i < maxParticles; ++i)
        lives[i] = 0;

    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
}

void ParticleSystem::genBaseMesh() {
    /* scale base mesh */
    for(int i = 0; i < sizeof(vertices) / sizeof(float); i++)
        vertices[i] *= particleSize;

}
void ParticleSystem::uploadBaseMesh() {
    glUseProgram(renderProg);

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    /* upload static base mesh */
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, base_mesh);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices),
            vertices, GL_STATIC_DRAW);
    glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDisableVertexAttribArray(0);

    glUseProgram(0);
}

void ParticleSystem::initGLPart() {
    loadGLPrograms();
    createBuffers();

    using namespace std::placeholders;
    initParticleBuffer();
    initLifeInfoBuffer();
    genBaseMesh();
    uploadBaseMesh();
}

void ParticleSystem::setParticleColor(glm::vec4 scol,
        glm::vec4 ecol) {

    glUseProgram(computeProg);
    glUniform4fv(2, 1, &scol[0]);
    glUniform4fv(3, 1, &ecol[0]);

    auto tmp = (ecol - scol) / float(particleLife);
    glUniform4fv(4, 1, &tmp[0]);
}

ParticleSystem::ParticleSystem() {}
ParticleSystem::ParticleSystem(float size,  size_t _maxp,
            size_t _pspawn, size_t _plife, size_t _respInterval) {

    particleSize = size;

    maxParticles    = _maxp;
    partSpawn       = _pspawn;
    particleLife    = _plife;
    respawnInterval = _respInterval;

    initGLPart();
    setParticleColor(glm::vec4(0, 0, 1, 1), glm::vec4(1, 0, 0, 1));
}

ParticleSystem::~ParticleSystem() {

    glDeleteBuffers(1, &particleSSbo);
    glDeleteBuffers(1, &lifeInfoSSbo);
    glDeleteBuffers(1, &base_mesh);

    glUseProgram(renderProg);
    glDeleteVertexArrays(1, &vao);
    glUseProgram(0);

    glDeleteProgram(renderProg);
    glDeleteProgram(computeProg);
}

void ParticleSystem::pause () {spawnNew = false;}
void ParticleSystem::resume() {spawnNew = true; }

void ParticleSystem::simulate() {
    glUseProgram(computeProg);

    if(currentIteration++ % respawnInterval == 0 && spawnNew) {
        glUseProgram(computeProg);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, lifeInfoSSbo);
        auto lives =  (uint*) glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0,
                sizeof(GLint),
                GL_MAP_WRITE_BIT | GL_MAP_READ_BIT);

        size_t sp_num = partSpawn, i = 0;

        while(i < maxParticles && sp_num > 0) {
            if(lives[i] == 0) {
                lives[i] = 1;
                --sp_num;
            }
            ++i;
        }


        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, particleSSbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, lifeInfoSSbo);

    glDispatchComputeGroupSizeARB(NUM_WORKGROUPS, 1, 1,
                    WORKGROUP_SIZE, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}


/* TODO: use glDrawElementsInstanced instead of glDrawArraysInstanced */
void ParticleSystem::render() {

    glUseProgram(renderProg);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(vao);

    /* prepare vertex attribs */
    glEnableVertexAttribArray(0);
    glBindBuffer (GL_ARRAY_BUFFER, base_mesh);
    glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, particleSSbo);
    glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)sizeof(float));

    glEnableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, particleSSbo);
    glVertexAttribPointer (2, 4, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)(5 * sizeof(float)));

    glVertexAttribDivisor(0, 0);
    glVertexAttribDivisor(1, 1);
    glVertexAttribDivisor(2, 1);

    /* draw particles */
    glDrawArraysInstanced (GL_TRIANGLES, 0, 6,
            maxParticles);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);

    glUseProgram(0);
}

