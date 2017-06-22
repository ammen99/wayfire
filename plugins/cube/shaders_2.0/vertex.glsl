#version 100

attribute mediump vec2 position;
attribute highp vec2 uvPosition;

varying highp vec2 uvpos;

uniform mat4 VP;
uniform mat4 model;

void main() {
    gl_Position = VP * model * vec4(position, 0.0, 1.0);
    uvpos = uvPosition;
}
