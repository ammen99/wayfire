#version 330

in vec3 position;
in vec2 uvPosition;

out vec2 uvpos;

uniform mat4 VP;
uniform mat4 initialModel;

void main() {
    gl_Position = VP * initialModel * vec4(position, 1.0);
    uvpos = uvPosition;
}
