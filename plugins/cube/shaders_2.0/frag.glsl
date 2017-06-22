#version 100

varying highp vec2 uvpos;
uniform sampler2D smp;

void main() {
    gl_FragColor = vec4(texture2D(smp, uvpos).xyz, 1);
}
