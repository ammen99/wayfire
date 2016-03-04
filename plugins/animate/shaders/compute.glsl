#version 440
#extension GL_ARB_compute_variable_group_size : enable

#define PARTICLE_DEAD  0
#define PARTICLE_RESP  1
#define PARTICLE_ALIVE 2

layout(location = 1) uniform float maxLife;

layout(location = 2) uniform vec4 scol;
layout(location = 3) uniform vec4 ecol;
/* colDiffStep = (ecol - scol) / maxLife */;
layout(location = 4) uniform vec4 colDiffStep;

struct Particle {
    float life;
    float x, y;
    float dx, dy;
    float r, g, b, a;
};

layout(binding = 1) buffer Particles {
    Particle _particles[];
};

layout(binding = 2) buffer ParticleLifeInfo {
    uint lifeInfo[];
};


layout(local_size_variable) in;


void main() {
    uint i = gl_GlobalInvocationID.x;
    Particle p = _particles[i];

    if(p.life >= maxLife) {
        if(lifeInfo[i] == PARTICLE_RESP) {

            p.x = p.y = 0;
            p.life = 0;

            p.r = scol.x;
            p.g = scol.y;
            p.b = scol.z;
            p.a = scol.w;

            lifeInfo[i] == PARTICLE_ALIVE;
        }
        else {
            lifeInfo[i] = PARTICLE_DEAD;
            return;
        }
    }

    p.life += 1;

    p.x += p.dx;
    p.y += p.dy;

    p.r += colDiffStep.x;
    p.g += colDiffStep.y;
    p.b += colDiffStep.z;
    p.a += colDiffStep.w;

    _particles[i] = p;
}

