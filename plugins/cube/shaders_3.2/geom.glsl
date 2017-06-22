#version 320 es

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

in vec2 tesuv[3];
in vec3 tePosition[3];

uniform mat3 NM;
uniform int  light;

out vec2 guv;
out vec3 colorFactor;

#define AL 0.2    // ambient lighting
#define DL (1.0-AL) // diffuse lighting

void main() {

    if(light == 1) {
        vec3 A = tePosition[2] - tePosition[0];
        vec3 B = tePosition[1] - tePosition[0];
        vec3 N = normalize(NM * normalize(cross(A, B)));
        vec3 L = vec3(0, 0, 1.5);

        float df = AL + DL * clamp(abs(dot(N, L)), 0.0, 1.0);
        colorFactor = vec3(df, df, df);
    }
    else
        colorFactor = vec3(1.0, 1.0, 1.0);

    gl_Position = gl_in[0].gl_Position;
    guv = tesuv[0];
    EmitVertex();

    gl_Position = gl_in[1].gl_Position;
    guv = tesuv[1];
    EmitVertex();

    gl_Position = gl_in[2].gl_Position;
    guv = tesuv[2];
    EmitVertex();
}
