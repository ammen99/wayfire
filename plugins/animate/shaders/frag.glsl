#version 310 es

in mediump vec4 out_color;
in mediump vec2 pos;
out mediump vec4 fragColor;

layout(location = 4) uniform highp float radii;

void main()
{
    mediump float dist_center = sqrt(pos.x * pos.x + pos.y * pos.y);
    mediump float factor = (radii - dist_center) / radii;
    if (factor < 0.0) factor = 0.0;
    mediump float factor2 = factor * factor;

    fragColor = vec4(out_color.xyz, out_color.w * factor2);
}
