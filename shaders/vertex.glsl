#version 100

attribute mediump vec2 position;
attribute highp vec2 uvPosition;

varying highp vec2 uvpos;

uniform mat4 MVP;

void main() {

    gl_Position = MVP * vec4(position.xy, 0.0, 1.0);
//    gl_Position.x /= 4.0;
//   gl_Position.y /= 4.0;

    uvpos = uvPosition;
}
