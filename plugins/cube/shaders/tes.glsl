#version 400
layout(triangles) in;

in vec3 tcPosition[];
in vec2 uv[];

out vec2 tesuv;
out vec3 tePosition;

uniform mat4 initialModel;
uniform mat4 VP;
uniform int  deform;

vec2 interpolate2D(vec2 v0, vec2 v1, vec2 v2) {
    return vec2(gl_TessCoord.x) * v0
         + vec2(gl_TessCoord.y) * v1
         + vec2(gl_TessCoord.z) * v2;
}

vec3 interpolate3D(vec3 v0, vec3 v1, vec3 v2) {
    return vec3(gl_TessCoord.x) * v0
         + vec3(gl_TessCoord.y) * v1
         + vec3(gl_TessCoord.z) * v2;
}


vec3 tp;
void main() {
    tesuv = interpolate2D(uv[0], uv[1], uv[2]);

    tp = interpolate3D(tcPosition[0], tcPosition[1], tcPosition[2]);
    tp = (initialModel * vec4(tp, 1.0)).xyz;

    if(deform > 0) {
        float r = 0.5;
        float d = distance(tp.xz, vec2(0, 0));
        float scale = 1;
        if(deform == 1)
            scale = r / d;
        else
            scale = d / r;
        tp = vec3(tp[0] * scale, tp[1], tp[2] * scale);
    }

    tePosition = tp;
    gl_Position = VP * vec4 (tp, 1.0);
}
