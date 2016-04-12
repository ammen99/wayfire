#version 100

varying highp vec2 uvpos;


uniform sampler2D smp;
uniform mediump vec4      color;

//out mediump vec4 gl_FragColor;


void main() {
    //if(depth == 32)
        gl_FragColor = texture2D(smp, uvpos) * color;
//        gl_FragColor = vec4(1., 1., 1., 1.);
    //else
    //gl_FragColor = vec4(1, 0, 0, 1);
//        gl_FragColor = vec4(texture2D(smp, uvpos).xyz, 1);
    //if(bgra == 1)
    //    gl_FragData[0] = gl_FragData[0].zyxw;
}
