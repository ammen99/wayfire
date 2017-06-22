#version 320 es

in vec3 position;
in vec2 uvPosition;

out vec2 uvpos;
out vec3 vPos;

void main() {
    vPos = position;
    uvpos = uvPosition;
}
