#version 330
#extension GL_ARB_explicit_uniform_location : enable

in vec4 out_color;
in vec2 pos;
out vec4 outColor;

layout(location = 1) uniform float size;

void main() {
    float val = 10 * size / sqrt(pos.x * pos.x + pos.y * pos.y);
    outColor = vec4(out_color.xyz, out_color.w * val);
   // outColor = vec4(out_color.xyz, 1.0);
//    outColor = vec4(1, 1, 1, 1);
}
