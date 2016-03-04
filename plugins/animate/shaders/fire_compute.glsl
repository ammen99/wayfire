#version 440
#extension GL_ARB_compute_variable_group_size : enable

#define PARTICLE_DEAD 0
#define PARTICLE_RESP  1
#define PARTICLE_ALIVE 2
#define PARTICLE_AFTER_LIVING 3

layout(location = 1) uniform float maxLife;

layout(location = 2) uniform vec4 scol;
layout(location = 3) uniform vec4 ecol;
layout(location = 4) uniform vec4 colDiffStep;

layout(location = 5) uniform float maxw;
layout(location = 6) uniform float maxh;

/* used only for Upwards movement */
layout(location = 7) uniform float wind;

struct Particle {
    float life;
    float x, y;
    float dx, dy;
    //vec4 col;
    float r, g, b, a;
};

layout(binding = 1) buffer Particles {
    Particle _particles[];
};

layout(binding = 2) buffer ParticleLifeInfo {
    uint lifeInfo[];
};


layout(local_size_variable) in;

float rand(vec2 co){
    return fract(sin(dot(co.xy, vec2(12.9898,78.233))) * 43758.5453);
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    Particle p = _particles[i];

    if(abs(maxLife - p.life) <= 1e-5) {
        lifeInfo[i] = PARTICLE_AFTER_LIVING;
    }

    if(p.life > maxLife) {
        if(lifeInfo[i] == PARTICLE_RESP) {

            //            p.x = p.y = 0;
            p.life = 0;

         p.r = scol.x;
         p.g = scol.y;
         p.b = scol.z;
         p.a = scol.w;

            //p.col = scol;
            lifeInfo[i] = PARTICLE_ALIVE;
        }
        else if(lifeInfo[i] == PARTICLE_AFTER_LIVING) {
            p.x = -100;
            p.y = -100;
            _particles[i] = p;
            lifeInfo[i] = PARTICLE_AFTER_LIVING;
            return;
        }
    }

    if(lifeInfo[i] == PARTICLE_ALIVE ||
       lifeInfo[i] == PARTICLE_AFTER_LIVING) {

        p.life += 1;

        p.y += p.dy + wind;
        p.y += (rand(vec2(p.y, p.x)) - 0.5) * abs(maxh) / 20.;

//        p.col += colDiffStep;
        p.r += colDiffStep.x;
        p.g += colDiffStep.y;
        p.b += colDiffStep.z;
        p.a += colDiffStep.w;
    }

    _particles[i] = p;
}
