#version 320 es

in highp vec2 guv;
in highp vec3 colorFactor;
layout(location = 0) out mediump vec4 outColor;

uniform sampler2D smp;

void main() {
    outColor = vec4(texture(smp, guv).xyz * colorFactor, 1);
}
