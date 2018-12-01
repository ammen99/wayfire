#include "blur.hpp"

static const char* gaussian_vertex_shader =
R"(
#version 100

attribute mediump vec2 position;
attribute mediump vec2 texcoord;
uniform vec2 size;
uniform float offset;

varying highp vec2 blurcoord[10];

uniform mat4 mvp;

void main() {
    gl_Position = vec4(position.xy, 0.0, 1.0);

    blurcoord[0] = texcoord;
    blurcoord[1] = texcoord + vec2(1.0 * offset) / size;
    blurcoord[2] = texcoord - vec2(1.0 * offset) / size;
    blurcoord[3] = texcoord + vec2(2.0 * offset) / size;
    blurcoord[4] = texcoord - vec2(2.0 * offset) / size;
    blurcoord[5] = texcoord + vec2(3.0 * offset) / size;
    blurcoord[6] = texcoord - vec2(3.0 * offset) / size;
    blurcoord[7] = texcoord + vec2(4.0 * offset) / size;
    blurcoord[8] = texcoord - vec2(4.0 * offset) / size;
    blurcoord[9] = vec4(mvp * vec4(texcoord - 0.5, 0.0, 1.0)).xy + 0.5;
}
)";

static const char* gaussian_fragment_shader =
R"(
#version 100
precision mediump float;

uniform sampler2D window_texture;
uniform sampler2D bg_texture;
uniform int mode;

varying highp vec2 blurcoord[10];

void main()
{
    vec2 uv = blurcoord[0];

    if (mode == 0) {
        vec4 bp = vec4(0.0);
        bp += texture2D(bg_texture, vec2(blurcoord[0].x, uv.y)) * 0.2270270270;
        bp += texture2D(bg_texture, vec2(blurcoord[1].x, uv.y)) * 0.1945945946;
        bp += texture2D(bg_texture, vec2(blurcoord[2].x, uv.y)) * 0.1945945946;
        bp += texture2D(bg_texture, vec2(blurcoord[3].x, uv.y)) * 0.1216216216;
        bp += texture2D(bg_texture, vec2(blurcoord[4].x, uv.y)) * 0.1216216216;
        bp += texture2D(bg_texture, vec2(blurcoord[5].x, uv.y)) * 0.0540540541;
        bp += texture2D(bg_texture, vec2(blurcoord[6].x, uv.y)) * 0.0540540541;
        bp += texture2D(bg_texture, vec2(blurcoord[7].x, uv.y)) * 0.0162162162;
        bp += texture2D(bg_texture, vec2(blurcoord[8].x, uv.y)) * 0.0162162162;
        gl_FragColor = vec4(bp.rgb, 1.0);
    } else if (mode == 1) {
        vec4 bp = vec4(0.0);
        bp += texture2D(bg_texture, vec2(uv.x, blurcoord[0].y)) * 0.2270270270;
        bp += texture2D(bg_texture, vec2(uv.x, blurcoord[1].y)) * 0.1945945946;
        bp += texture2D(bg_texture, vec2(uv.x, blurcoord[2].y)) * 0.1945945946;
        bp += texture2D(bg_texture, vec2(uv.x, blurcoord[3].y)) * 0.1216216216;
        bp += texture2D(bg_texture, vec2(uv.x, blurcoord[4].y)) * 0.1216216216;
        bp += texture2D(bg_texture, vec2(uv.x, blurcoord[5].y)) * 0.0540540541;
        bp += texture2D(bg_texture, vec2(uv.x, blurcoord[6].y)) * 0.0540540541;
        bp += texture2D(bg_texture, vec2(uv.x, blurcoord[7].y)) * 0.0162162162;
        bp += texture2D(bg_texture, vec2(uv.x, blurcoord[8].y)) * 0.0162162162;
        gl_FragColor = vec4(bp.rgb, 1.0);
    } else {
        vec4 wp = texture2D(window_texture, blurcoord[9]);
        vec4 bp = texture2D(bg_texture, uv);
        vec4 c = clamp(4.0 * wp.a, 0.0, 1.0) * bp;
        gl_FragColor = wp + (1.0 - wp.a) * c;
    }
}
)";

static GLuint gauss_prog, posID, mvpID, texID[2], texcoordID, modeID, sizeID, offsetID;

void
wayfire_gaussian_blur::init()
{
    OpenGL::render_begin();
    auto vs = OpenGL::compile_shader(gaussian_vertex_shader, GL_VERTEX_SHADER);
    auto fs = OpenGL::compile_shader(gaussian_fragment_shader, GL_FRAGMENT_SHADER);

    gauss_prog = GL_CALL(glCreateProgram());
    GL_CALL(glAttachShader(gauss_prog, vs));
    GL_CALL(glAttachShader(gauss_prog, fs));
    GL_CALL(glLinkProgram(gauss_prog));

    posID = GL_CALL(glGetAttribLocation(gauss_prog, "position"));
    texcoordID = GL_CALL(glGetAttribLocation(gauss_prog, "texcoord"));
    mvpID = GL_CALL(glGetUniformLocation(gauss_prog, "mvp"));
    sizeID  = GL_CALL(glGetUniformLocation(gauss_prog, "size"));
    offsetID  = GL_CALL(glGetUniformLocation(gauss_prog, "offset"));
    modeID  = GL_CALL(glGetUniformLocation(gauss_prog, "mode"));
    texID[0] = GL_CALL(glGetUniformLocation(gauss_prog, "window_texture"));
    texID[1] = GL_CALL(glGetUniformLocation(gauss_prog, "bg_texture"));

    /* won't be really deleted until program is deleted as well */
    GL_CALL(glDeleteShader(vs));
    GL_CALL(glDeleteShader(fs));
    OpenGL::render_end();
}

void
wayfire_gaussian_blur::pre_render(uint32_t src_tex,
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

    /* Enable our shader and pass some data to it. The shader accepts two textures
     * and does box blur on the background texture in two passes, one horizontal
     * and one vertical */
    GL_CALL(glUseProgram(gauss_prog));
    GL_CALL(glUniform1i(texID[0], 0));
    GL_CALL(glUniform1i(texID[1], 1));
    GL_CALL(glUniform2f(sizeID, sw, sh));
    GL_CALL(glUniform1f(offsetID, offset));

    GL_CALL(glUniformMatrix4fv(mvpID, 1, GL_FALSE, &glm::mat4(1.0)[0][0]));
    GL_CALL(glVertexAttribPointer(posID, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
    GL_CALL(glVertexAttribPointer(texcoordID, 2, GL_FLOAT, GL_FALSE, 0, texCoords));
    GL_CALL(glEnableVertexAttribArray(texcoordID));
    GL_CALL(glEnableVertexAttribArray(posID));

    for (i = 0; i < iterations; i++) {
        /* Tell shader to blur horizontally */
        GL_CALL(glUniform1i(modeID, 0));

        fb[1].allocate(sw, sh);
        fb[1].bind();

        /* Bind textures */
        GL_CALL(glActiveTexture(GL_TEXTURE0 + 0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, src_tex));
        GL_CALL(glActiveTexture(GL_TEXTURE0 + 1));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, fb[0].tex));

        /* Render to create horizontally blurred background image */
        GL_CALL(glViewport(0, 0, sw, sh));
        GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

        /* Setup another texture as rendering target */
        fb[0].allocate(sw, sh);
        fb[0].bind();

        /* Update second input texture to be output of last pass */
        GL_CALL(glActiveTexture(GL_TEXTURE0 + 1));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, fb[1].tex));

        /* Tell shader to blur vertically */
        GL_CALL(glUniform1i(modeID, 1));

        /* Render to target_fb with window texture and blurred background alpha blended */
        GL_CALL(glViewport(0, 0, sw, sh));
        GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
    }

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
wayfire_gaussian_blur::render(uint32_t src_tex,
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
    GL_CALL(glUseProgram(gauss_prog));
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

    GL_CALL(glViewport(x, fb_h - y - h, w, h));
    target_fb.scissor(scissor_box);

    /* Render to target_fb with window texture and blurred background alpha blended */
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
wayfire_gaussian_blur::fini()
{
    OpenGL::render_begin();
    GL_CALL(glDeleteProgram(gauss_prog));
    fb[0].release();
    fb[1].release();
    OpenGL::render_end();
}
