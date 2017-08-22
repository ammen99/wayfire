#version 310 es
#define NUM_WORKGROUPS 512
#define MY_PI 3.14159

layout(local_size_x = NUM_WORKGROUPS) in;

#define PARTICLE_DEAD 0
#define PARTICLE_RESP 1
#define PARTICLE_ALIVE 2

layout(location = 1) uniform int maxLife;

layout(location = 2) uniform vec4 scol;
layout(location = 3) uniform vec4 ecol;
layout(location = 4) uniform vec4 colDiffStep;

layout(location = 5) uniform float maxw;
layout(location = 6) uniform float maxh;

/* used only for Upwards movement */
layout(location = 7) uniform float gravity;

layout(location = 8) uniform float time;

uniform sampler2D img;

struct Particle
{
    float x, y;
    float dx, dy;
    float r, g, b, a;
    int life;
};

layout(std430, binding = 1) buffer Particles {
    Particle _particles[];
};

layout(std430, binding = 2) buffer ParticleLifeInfo {
    int lifeInfo[];
};

float rand(vec2 co)
{
    return fract(sin(dot(co.xy, vec2(12.9898,78.233))) * 43758.5453);
}

float noise3D(vec3 p)
{
    p.z = fract(p.z)*256.0;
    float iz = floor(p.z);
    float fz = fract(p.z);
    vec2 a_off = vec2(23.0, 29.0)*(iz)/256.0;
    vec2 b_off = vec2(23.0, 29.0)*(iz+1.0)/256.0;
    float a = texture(img, p.xy + a_off).r;
    float b = texture(img, p.xy + b_off).r;
    return mix(a, b, fz);
}

float perlinNoise3D(vec3 p)
{
    float zz = p.z;
    float xx = p.x;
    p.z = xx;
    p.x = zz;

    float x = 0.0;
    for (float i = 0.0; i < 6.0; i += 1.0)
        x += noise3D(p * pow(2.0, i)) * pow(0.5, i);
    return x;
}

void main()
{
    uint i = gl_GlobalInvocationID.x;

    Particle p = _particles[i];
    if (lifeInfo[i] == PARTICLE_RESP)
    {
        p.life = 0;
        p.y = 0.0;

        p.r = scol.x;
        p.g = scol.y;
        p.b = scol.z;
        p.a = scol.w;

        lifeInfo[i] = PARTICLE_ALIVE;

        _particles[i] = p;
        return;
    }

    if(p.life > maxLife || lifeInfo[i] == PARTICLE_DEAD)
    {
        lifeInfo[i] = PARTICLE_DEAD;
        _particles[i].y = 1000.0;
        return;
    }

    const float offset = 0.007;
    const float delta = 0.002;

    p.y += p.dy;
    p.y += (rand(vec2(p.y, p.x)) - 0.5) * maxh / 100.;
    p.x += p.dx;
    p.dy += gravity;

    float bx = p.x / maxw;
    float by = p.y / maxh;

    float v1 = perlinNoise3D(vec3(bx, by + offset, time));
    float v2 = perlinNoise3D(vec3(bx + offset, by, time));
    float v3 = perlinNoise3D(vec3(bx - offset, by, time));

    float m = max(max(v1, v2), v3);

    bool go_right = (v2 == m && p.x <= maxw);
    bool go_left  = (v1 == m && p.x >= 0.0);

    if (go_left && go_right)
    {
        int lr = int(rand(vec2(p.x, p.y)) * 1000.0);
        if (lr > 499)
        {
            go_left = false;
        } else
        {
            go_right = false;
        }
    }

    if (go_left)
    {
        p.x += delta * maxw;
    } else if (go_right)
    {
        p.x -= delta * maxw;
    } else
    {
        p.y += delta * maxh * 0.5;
    }

    float rand_dx = (rand(vec2(p.y, p.x)) - 0.5) * 0.01 * maxw;
    if (p.x + rand_dx <= maxw * 1.01 && p.x + rand_dx >= 0.01)
        p.x += rand_dx;
    else
        p.x -= rand_dx;

    p.life += 1;
    _particles[i] = p;
}
