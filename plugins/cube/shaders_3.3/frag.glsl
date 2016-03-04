#version 330

in vec2 uvpos;
layout(location = 0) out vec4 outColor;

uniform sampler2D smp;

void main() {
    outColor = vec4(texture(smp, uvpos).zyx, 1);
}
