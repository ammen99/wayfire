#version 400

in vec2 guv;
in vec3 colorFactor;
layout(location = 0) out vec4 outColor;

uniform sampler2D smp;

void main() {
    outColor = vec4(texture(smp, guv).zyx * colorFactor, 1);
}
