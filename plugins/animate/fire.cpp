#include <opengl.hpp>
#include <cmath>
#include "fire.hpp"

#define EFFECT_CYCLES 16
#define BURSTS 10
#define MAX_PARTICLES 1024 * 384
#define PARTICLE_SIZE 0.006
#define MAX_LIFE 32
#define RESP_INTERVAL 1

bool run = true;

#define avg(x,y) (((x) + (y))/2.0)
#define clamp(t,x,y) t=(t > (y) ? (y) : (t < (x) ? (x) : t))

/* a set of windows we are animating,
 * so that we don't create 2+ particle systems
 * for the same window */
std::unordered_set<Window> animating_windows;

class FireParticleSystem : public ParticleSystem {
    float _cx, _cy;
    float _w, _h;

    float wind, gravity;

    public:

    void loadComputeProgram() {
        std::string shaderSrcPath = "/usr/local/share/fireman/animate/shaders";

        computeProg = glCreateProgram();
        GLuint css =
            GLXUtils::loadShader(std::string(shaderSrcPath)
                    .append("/fire_compute.glsl").c_str(),
                    GL_COMPUTE_SHADER);

        glAttachShader(computeProg, css);
        glLinkProgram(computeProg);
        glUseProgram(computeProg);

        glUniform1f(1, particleLife);
        glUniform1f(5, _w);
        glUniform1f(6, _h);
    }

    void genBaseMesh() {
        ParticleSystem::genBaseMesh();
        for(int i = 0; i < 6; i++)
            vertices[2 * i + 0] += _cx - _w,
            vertices[2 * i + 1] += _cy - _h;
    }

    void defaultParticleIniter(Particle &p) {
        p.life = particleLife + 1;

        p.dy = 2. * _h * float(std::rand() % 1001) / (1000 * EFFECT_CYCLES);
        p.dx = 0;

        p.x = (float(std::rand() % 1001) / 1000.0) * _w * 2.;
        p.y = 0;

        p.r = p.g = p.b = p.a = 0;
    }

    FireParticleSystem(float cx, float cy,
            float w, float h,
            int numParticles) :
        _cx(cx), _cy(cy), _w(w), _h(h) {

            particleSize = PARTICLE_SIZE;

            gravity = -_h * 0.001;
            wind = -gravity * RESP_INTERVAL * BURSTS * 2;

            maxParticles    = numParticles;
            partSpawn       = numParticles / BURSTS;
            particleLife    = MAX_LIFE;
            respawnInterval = RESP_INTERVAL;

            initGLPart();
            setParticleColor(glm::vec4(0, 0.5, 1, 1), glm::vec4(0, 0, 0.7, 0.2));

            glUseProgram(renderProg);
            glUniform1f(1, particleSize);
            glUseProgram(0);

            //wind = 0;
        }

    /* checks if PS should run further
     * and if we should stop spawning */
    int numSpawnedBursts = 0;

    /* make particle system report it has stopped */
    void disable() {
        currentIteration = EFFECT_CYCLES + 1;
    }

    bool check() {

        if((currentIteration - 1) % respawnInterval == 0)
            ++numSpawnedBursts;

        if(numSpawnedBursts >= BURSTS)
            pause();

        return currentIteration <= EFFECT_CYCLES;
    }

    void simulate() {
        glUseProgram(computeProg);
        glUniform1f(7, wind);
        ParticleSystem::simulate();

        wind += gravity;
    }

    void addOffset(float dx, float dy) {
        for(int i = 0; i < 6; ++i) {
            vertices[2 * i + 0] += dx;
            vertices[2 * i + 1] += dy;
        }

        uploadBaseMesh();
    }
};
bool first_time = true;

struct DeleteFireObjectHook {

};

Fire::Fire(View win) : w(win) {
    auto x = win->attrib.x,
         y = win->attrib.y,
         w = win->attrib.width,
         h = win->attrib.height;

    GetTuple(sw, sh, core->getScreenSize());

    float w2 = float(sw) / 2.,
          h2 = float(sh) / 2.;

    float tlx = float(x) / w2 - 1.,
          tly = 1. - float(y) / h2;

    float brx = tlx + w / w2,
          bry = tly - h / h2;


    int numParticles =
        MAX_PARTICLES *  w / float(sw) * h / float(sh);

    std::cout << "INITIATING WITH " << numParticles << " " << numParticles / 384 << std::endl;
    ps = new FireParticleSystem(avg(tlx, brx), avg(tly, bry),
            w / float(sw), h / float(sh), numParticles);

    hook.action = std::bind(std::mem_fn(&Fire::step), this);
    hook.type = EFFECT_WINDOW;
    hook.win = win;
    core->addEffect(&hook);
    hook.enable();

    win->transform.color[3] = 0;
    transparency.action = std::bind(std::mem_fn(&Fire::adjustAlpha), this);
    core->addHook(&transparency);
    transparency.enable();

    moveListener.action =
        std::bind(std::mem_fn(&Fire::handleWindowMoved),
                this, std::placeholders::_1);
    core->connect_signal("move-window", &moveListener);


    if(animating_windows.find(this->w->id) != animating_windows.end()) {
        ps->disable();
    }
    else {
        animating_windows.insert(this->w->id);
    }

    unmapListener.action =
        std::bind(std::mem_fn(&Fire::handleWindowUnmapped),
                this, std::placeholders::_1);
    core->connect_signal("unmap-window", &unmapListener);

    /* TODO : Check if necessary */
    core->setRedrawEverything(true);
    OpenGL::useDefaultProgram();
}

void Fire::step() {
    ps->simulate();

    if(w->isVisible())
        ps->render();
    OpenGL::useDefaultProgram();
//
    if(!ps->check()) {
        w->transform.color[3] = 1;

        core->remHook(transparency.id);
        core->remEffect(hook.id, w);
        core->disconnect_signal("move-window", moveListener.id);
        core->disconnect_signal("unmap-window", unmapListener.id);

        animating_windows.erase(w->id);
        delete this;
    }
}

void Fire::adjustAlpha() {

    float c = float(progress) / float(EFFECT_CYCLES);
    c = std::pow(100, c) / 100.;

    w->transform.color[3] = c;
    ++progress;
}

void Fire::handleWindowMoved(SignalListenerData d) {
    FireWin *w = (FireWin*)d[0];
    if(w->id != this->w->id)
        return;

    int dx = *(int*)d[1];
    int dy = *(int*)d[2];

    GetTuple(sw, sh, core->getScreenSize());

    float fdx = 2. * float(dx) / float(sw);
    float fdy = 2. * float(dy) / float(sh);

    ps->addOffset(fdx, -fdy);
    OpenGL::useDefaultProgram();
}

void Fire::handleWindowUnmapped(SignalListenerData d) {
    View ww = *(FireWindow*)d[0];
    if(ww->id == w->id) {
        ps->disable();
        step();
    }
}

struct RedrawOnceMoreHook {
    Hook h;
    RedrawOnceMoreHook() {
        h.action = std::bind(std::mem_fn(&RedrawOnceMoreHook::step), this);
        core->addHook(&h);
        h.enable();
    }

    void step() {
        core->damageRegion(core->getMaximisedRegion());
        core->remHook(h.id);
        delete this;
        OpenGL::useDefaultProgram();
    }
};

Fire::~Fire() {
    delete ps;
    core->setRedrawEverything(false);
    core->damageRegion(core->getMaximisedRegion());

    new RedrawOnceMoreHook();
}
