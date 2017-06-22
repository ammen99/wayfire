#version 400

layout(vertices = 3) out;

in vec2 uvpos[];
in vec3 vPos[];

out vec3 tcPosition[];
out vec2 uv[];

#define ID gl_InvocationID

uniform int deform;
uniform int light;

void main() {

    tcPosition[ID] = vPos[ID];
    uv[ID] = uvpos[ID];

    if(ID == 0){
        /* deformation requires tesselation
           and lighting even higher degree to
           make lighting smoother */

        int tessLevel = 1;
        if(deform > 0)
            tessLevel = 30;
        if(light > 0)
            tessLevel = 50;

        gl_TessLevelInner[0] = tessLevel;
        gl_TessLevelOuter[0] = tessLevel;
        gl_TessLevelOuter[1] = tessLevel;
        gl_TessLevelOuter[2] = tessLevel;
    }
}
