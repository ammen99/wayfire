#version 310 es

layout(location = 0) in mediump vec2 position;
layout(location = 1) in mediump vec2 center;
layout(location = 2) in mediump vec4 color;
layout(location = 3) uniform mediump vec2 global_offset;

out mediump vec4 out_color;
out mediump vec2 pos;

void main() {
    gl_Position = vec4 (position + center + global_offset, 0.0, 1.0);
    out_color = color;
    pos = position;
}
