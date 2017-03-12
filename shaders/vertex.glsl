#version 100

attribute mediump vec3 position;
attribute highp vec2 uvPosition;

varying highp vec2 uvpos;

uniform mat4 MVP;
uniform float w2;
uniform float h2;

uniform int time;

void main() {

    gl_Position = vec4(position.x / w2, position.y / h2, position.z, 1.0);
    //gl_Position = vec4(position, 1.0);
    //gl_Position = MVP * vec4(, 1.0);
    //gl_Position = ortho * vec4(position, 1.0);
    uvpos = uvPosition;
}
