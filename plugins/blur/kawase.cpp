#include "blur.hpp"

static const char* kawase_vertex_shader =
R"(
#version 100

attribute mediump vec2 position;
attribute mediump vec2 texcoord;

varying mediump vec2 uvpos[2];

uniform mat4 mvp;

void main() {

    gl_Position = vec4(position.xy, 0.0, 1.0);
    uvpos[0] = texcoord;
    uvpos[1] = vec4(mvp * vec4(texcoord - 0.5, 0.0, 1.0)).xy + 0.5;
}
)";

static const char* kawase_fragment_shader =
R"(
#version 100
precision mediump float;

uniform float offset;
uniform vec2 halfpixel;
uniform sampler2D window_texture;
uniform sampler2D bg_texture;
uniform int mode;

varying mediump vec2 uvpos[2];

void main()
{
    vec2 uv = uvpos[0];

    if (mode == 0) {
        vec4 sum = texture2D(bg_texture, uv + vec2(-halfpixel.x * 2.0, 0.0) * offset);
        sum += texture2D(bg_texture, uv + vec2(-halfpixel.x, halfpixel.y) * offset) * 2.0;
        sum += texture2D(bg_texture, uv + vec2(0.0, halfpixel.y * 2.0) * offset);
        sum += texture2D(bg_texture, uv + vec2(halfpixel.x, halfpixel.y) * offset) * 2.0;
        sum += texture2D(bg_texture, uv + vec2(halfpixel.x * 2.0, 0.0) * offset);
        sum += texture2D(bg_texture, uv + vec2(halfpixel.x, -halfpixel.y) * offset) * 2.0;
        sum += texture2D(bg_texture, uv + vec2(0.0, -halfpixel.y * 2.0) * offset);
        sum += texture2D(bg_texture, uv + vec2(-halfpixel.x, -halfpixel.y) * offset) * 2.0;
        gl_FragColor = sum / 12.0;
    } else if (mode == 1) {
        vec4 sum = texture2D(bg_texture, uv) * 4.0;
        sum += texture2D(bg_texture, uv - halfpixel.xy * offset);
        sum += texture2D(bg_texture, uv + halfpixel.xy * offset);
        sum += texture2D(bg_texture, uv + vec2(halfpixel.x, -halfpixel.y) * offset);
        sum += texture2D(bg_texture, uv - vec2(halfpixel.x, -halfpixel.y) * offset);
        gl_FragColor = sum / 8.0;
    } else {
        vec4 wp = texture2D(window_texture, uvpos[1]);
        vec4 bp = texture2D(bg_texture, uv);
        vec4 c = clamp(4.0 * wp.a, 0.0, 1.0) * bp;
        gl_FragColor = wp + (1.0 - wp.a) * c;
    }
}
)";

static GLuint kawase_prog, modeID, posID, mvpID, texcoordID, offsetID, halfpixelID, texID[2];

void
wayfire_kawase_blur::init()
{
    OpenGL::render_begin();
    auto vs = OpenGL::compile_shader(kawase_vertex_shader, GL_VERTEX_SHADER);
    auto fs = OpenGL::compile_shader(kawase_fragment_shader, GL_FRAGMENT_SHADER);

    kawase_prog = GL_CALL(glCreateProgram());
    GL_CALL(glAttachShader(kawase_prog, vs));
    GL_CALL(glAttachShader(kawase_prog, fs));
    GL_CALL(glLinkProgram(kawase_prog));

    posID = GL_CALL(glGetAttribLocation(kawase_prog, "position"));
    texcoordID = GL_CALL(glGetAttribLocation(kawase_prog, "texcoord"));
    mvpID = GL_CALL(glGetUniformLocation(kawase_prog, "mvp"));
    offsetID  = GL_CALL(glGetUniformLocation(kawase_prog, "offset"));
    halfpixelID  = GL_CALL(glGetUniformLocation(kawase_prog, "halfpixel"));
    modeID  = GL_CALL(glGetUniformLocation(kawase_prog, "mode"));
    texID[0] = GL_CALL(glGetUniformLocation(kawase_prog, "window_texture"));
    texID[1] = GL_CALL(glGetUniformLocation(kawase_prog, "bg_texture"));

    /* won't be really deleted until program is deleted as well */
    GL_CALL(glDeleteShader(vs));
    GL_CALL(glDeleteShader(fs));
    OpenGL::render_end();
}

static void
render_to_fbo(wf_framebuffer_base& in, wf_framebuffer_base& out, int width, int height)
{
    out.allocate(width, height);
    out.bind();

    GL_CALL(glActiveTexture(GL_TEXTURE0 + 1));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, in.tex));
    GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
}

void
wayfire_kawase_blur::pre_render(uint32_t src_tex,
                                wlr_box _src_box,
                                pixman_region32_t *damage,
                                const wf_framebuffer& target_fb,
                                struct blur_options *options)
{
    int i, iterations = options->iterations;
    float offset = options->offset;

    wlr_box fb_geom = target_fb.framebuffer_box_from_geometry_box(target_fb.geometry);

    pixman_box32_t box = *pixman_region32_extents(damage);
    wlr_box b;
    b.x = box.x1;
    b.y = box.y1;
    b.width = box.x2 - box.x1;
    b.height = box.y2 - box.y1;
    b = target_fb.framebuffer_box_from_damage_box(b);
    auto src_box = target_fb.framebuffer_box_from_geometry_box(_src_box);
    int fb_h = fb_geom.height;

    src_box.x -= fb_geom.x;
    src_box.y -= fb_geom.y;

    int x = src_box.x, y = src_box.y, w = src_box.width, h = src_box.height;
    int bx = b.x, by = b.y, bw = b.width, bh = b.height;

    int sw = bw * (1.0 / options->degrade);
    int sh = bh * (1.0 / options->degrade);

    int pw = sw * options->degrade;
    int ph = sh * options->degrade;

    static const float vertexData[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f
    };
    static const float texCoords[] = {
         0.0f, 0.0f,
         1.0f, 0.0f,
         1.0f, 1.0f,
         0.0f, 1.0f
    };

    OpenGL::render_begin(target_fb);

    /* The damage region we recieve as an argument to this function
     * contains last and current damage. We take the bounding box
     * of this region for blurring. At this point, target_fb contains
     * the scene rendered up until the view for which this function is
     * called. To save resources, the texture can be blurred at a
     * smaller size and then scaled back up. This causes discrepancies
     * between odd and even sizes so to even things out, we upscale
     * by one pixel in the odd size case when doing the initial blit. */
    fb[0].allocate(pw, ph);
    GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, target_fb.fb));
    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb[0].fb));
    /* The target_fb origin is at bottom left and the y is flipped so we have
     * to take these into account when blitting */
    GL_CALL(glBlitFramebuffer(bx, fb_h - by - bh, bx + bw, fb_h - by, 0, 0, pw, ph, GL_COLOR_BUFFER_BIT, GL_LINEAR));

    /* Upload data to shader */
    GL_CALL(glUseProgram(kawase_prog));
    GL_CALL(glUniformMatrix4fv(mvpID, 1, GL_FALSE, &glm::mat4(1.0)[0][0]));
    GL_CALL(glVertexAttribPointer(posID, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
    GL_CALL(glVertexAttribPointer(texcoordID, 2, GL_FLOAT, GL_FALSE, 0, texCoords));
    GL_CALL(glEnableVertexAttribArray(posID));
    GL_CALL(glEnableVertexAttribArray(texcoordID));
    GL_CALL(glUniform2f(halfpixelID, 0.5f / sw, 0.5f / sh));
    GL_CALL(glUniform1f(offsetID, offset));
    GL_CALL(glUniform1i(texID[0], 0));
    GL_CALL(glUniform1i(texID[1], 1));

    /* Downsample */
    GL_CALL(glUniform1i(modeID, 0));

    for (i = 0; i < iterations; i++)
        render_to_fbo(fb[i % 2], fb[1 - i % 2], sw, sh);

    /* Upsample */
    GL_CALL(glUniform1i(modeID, 1));

    for (i = iterations - 1; i >= 0; i--)
        render_to_fbo(fb[1 - i % 2], fb[i % 2], sw, sh);

    fb[1].allocate(w, h);
    GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[0].fb));
    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb[1].fb));
    GL_CALL(glBlitFramebuffer(0, 0, sw, sh,
                              bx - x,
                              h - (by - y) - bh,
                              (bx + bw) - x,
                              h - (by - y),
                              GL_COLOR_BUFFER_BIT, GL_LINEAR));

    /* Disable stuff */
    GL_CALL(glUseProgram(0));
    GL_CALL(glActiveTexture(GL_TEXTURE0));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
    GL_CALL(glDisableVertexAttribArray(posID));
    GL_CALL(glDisableVertexAttribArray(texcoordID));

    OpenGL::render_end();
}

void
wayfire_kawase_blur::render(uint32_t src_tex,
                              wlr_box _src_box,
                              wlr_box scissor_box,
                              const wf_framebuffer& target_fb)
{
    wlr_box fb_geom = target_fb.framebuffer_box_from_geometry_box(target_fb.geometry);
    auto src_box = target_fb.framebuffer_box_from_geometry_box(_src_box);
    int fb_h = fb_geom.height;
    src_box.x -= fb_geom.x;
    src_box.y -= fb_geom.y;

    int x = src_box.x, y = src_box.y, w = src_box.width, h = src_box.height;

    OpenGL::render_begin(target_fb);

    /* Use shader and enable vertex and texcoord data */
    GL_CALL(glUseProgram(kawase_prog));
    GL_CALL(glEnableVertexAttribArray(posID));
    GL_CALL(glEnableVertexAttribArray(texcoordID));

    /* Blend blurred background with window texture src_tex */
    GL_CALL(glUniform1i(modeID, 2));
    GL_CALL(glUniformMatrix4fv(mvpID, 1, GL_FALSE, &glm::inverse(target_fb.transform)[0][0]));
    GL_CALL(glActiveTexture(GL_TEXTURE0 + 0));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, src_tex));
    GL_CALL(glActiveTexture(GL_TEXTURE0 + 1));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, fb[1].tex));
    GL_CALL(glEnable(GL_BLEND));
    GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

    /* Render it to target_fb */
    target_fb.bind();

    GL_CALL(glViewport(x, fb_h - y - h, w, h));
    target_fb.scissor(scissor_box);

    GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

    /* Disable stuff */
    GL_CALL(glUseProgram(0));
    GL_CALL(glDisable(GL_BLEND));
    GL_CALL(glActiveTexture(GL_TEXTURE0));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
    GL_CALL(glDisableVertexAttribArray(posID));
    GL_CALL(glDisableVertexAttribArray(texcoordID));

    OpenGL::render_end();
}

void
wayfire_kawase_blur::fini()
{
    OpenGL::render_begin();
    GL_CALL(glDeleteProgram(kawase_prog));
    fb[0].release();
    fb[1].release();
    OpenGL::render_end();
}
