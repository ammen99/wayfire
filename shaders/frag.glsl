#version 100

varying highp vec2 uvpos;

uniform sampler2D smp;
uniform mediump vec4      color;

void main()
{
    gl_FragColor = texture2D(smp, uvpos) * color;
}
