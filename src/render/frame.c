#include "gl_api.h"
#include "pointy/render.h"
#include "../core/geometry_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct PPRenderer {
    SDL_Window *window; SDL_GLContext context; PPGL gl;
    GLuint program, vao, fbo, textures[4];
};
static bool error_set(PPError *error, int code, const char *message) {
    if (error) { error->code = code; snprintf(error->message, sizeof(error->message), "%s", message); }
    return false;
}
void pp_frame_destroy(PPFrame *frame) {
    if (!frame) return;
    free(frame->pixels); free(frame->content_alpha); memset(frame, 0, sizeof(*frame));
}
static const char *vertex_shader =
    "#version 150\n"
    "void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);"
    "gl_Position = vec4(p*2.0-1.0,0.0,1.0); }";
static const char *fragment_shader =
    "#version 150\n"
    "uniform sampler2D image; uniform sampler2D alpha;"
    "uniform vec2 destination_size; uniform vec2 source_size;"
    "uniform vec2 rotation; uniform float scale; uniform float opacity;"
    "out vec4 color; out vec4 coverage;"
    "void main() {"
    "vec2 d=vec2(gl_FragCoord.x,destination_size.y-gl_FragCoord.y)-destination_size*0.5;"
    "vec2 s=vec2(rotation.x*d.x+rotation.y*d.y,-rotation.y*d.x+rotation.x*d.y)/scale;"
    "vec2 uv=s/source_size+0.5;"
    "if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1)))) { color=vec4(0); coverage=vec4(0); }"
    "else { color=texture(image,uv)*opacity; coverage=vec4(texture(alpha,uv).r,0,0,1); } }";
bool pp_renderer_create(PPRenderer **out, PPError *error) {
    if (!out) return error_set(error, PP_ERROR_ARGUMENT, "Missing renderer output");
    *out = NULL;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return error_set(error, PP_ERROR_RENDER, SDL_GetError());
    PPRenderer *renderer = calloc(1, sizeof(*renderer));
    if (!renderer) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return error_set(error, PP_ERROR_MEMORY, "Cannot allocate renderer"); }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);
    renderer->window = SDL_CreateWindow("PointyPointer renderer", 1, 1, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (renderer->window) renderer->context = SDL_GL_CreateContext(renderer->window);
    if (!renderer->context || !SDL_GL_MakeCurrent(renderer->window, renderer->context)) {
        error_set(error, PP_ERROR_RENDER, SDL_GetError()); pp_renderer_destroy(renderer); return false;
    }
    if (!pp_gl_load(&renderer->gl, error)) { pp_renderer_destroy(renderer); return false; }
    PPGL *gl = &renderer->gl;
    renderer->program = pp_gl_program(gl, vertex_shader, fragment_shader, error);
    if (!renderer->program) { pp_renderer_destroy(renderer); return false; }
    gl->GenVertexArrays(1, &renderer->vao); gl->GenFramebuffers(1, &renderer->fbo);
    glGenTextures(4, renderer->textures);
    if (glGetError() != GL_NO_ERROR) { error_set(error,PP_ERROR_RENDER,"Cannot allocate GL frame resources"); pp_renderer_destroy(renderer); return false; }
    *out = renderer; return true;
}
static void texture(GLuint texture, GLint format, GLenum data_format, int w, int h, const void *data) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, data_format, GL_UNSIGNED_BYTE, data);
}
bool pp_renderer_transform(PPRenderer *renderer, const PPFrame *source, double longest_dip,
    double angle_deg, double opacity, double pixels_per_dip, PPFrame *out, PPError *error) {
    if (!renderer || !source || !source->pixels || !source->content_alpha || !out || out->pixels || out->content_alpha ||
        source->width <= 0 || source->height <= 0 || source->width > 16384 || source->height > 16384 ||
        source->stride < (size_t)source->width*4 || source->stride % 4 || source->stride/4 > 2147483647 ||
        !isfinite(longest_dip) || longest_dip <= 0 || !isfinite(angle_deg) || !isfinite(opacity) || opacity < 0 || opacity > 1 ||
        !isfinite(pixels_per_dip) || pixels_per_dip <= 0)
        return error_set(error, PP_ERROR_ARGUMENT, "Invalid frame transform");
    double scale = longest_dip*pixels_per_dip/(source->width > source->height ? source->width : source->height);
    double angle = fmod(angle_deg, 360)*3.14159265358979323846/180, c = cos(angle), s = sin(angle);
    int drawable_w,drawable_h;
    if (!pp_geometry_extent(source,longest_dip,angle_deg,pixels_per_dip,&drawable_w,&drawable_h))
        return error_set(error, PP_ERROR_ARGUMENT, "Transformed frame dimensions exceed limits");
    double w=drawable_w,h=drawable_h;
    PPFrame frame = {0}; frame.width = (int)w; frame.height = (int)h; frame.stride = (size_t)frame.width*4;
    size_t count = (size_t)frame.width*frame.height;
    frame.pixels = malloc(count*4); frame.content_alpha = malloc(count);
    uint8_t *readback = malloc(count*4);
    if (!frame.pixels || !frame.content_alpha || !readback) {
        pp_frame_destroy(&frame); free(readback); return error_set(error, PP_ERROR_MEMORY, "Cannot allocate transformed pixels");
    }
    if (!SDL_GL_MakeCurrent(renderer->window, renderer->context)) {
        pp_frame_destroy(&frame); free(readback); return error_set(error, PP_ERROR_RENDER, SDL_GetError());
    }
    PPGL *gl = &renderer->gl;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl->ActiveTexture(GL_TEXTURE0); glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(source->stride/4));
    texture(renderer->textures[0], GL_RGBA8, GL_RGBA, source->width,source->height,source->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); gl->ActiveTexture(GL_TEXTURE1);
    texture(renderer->textures[1], GL_R8, GL_RED, source->width,source->height,source->content_alpha);
    texture(renderer->textures[2], GL_RGBA8, GL_RGBA, frame.width,frame.height,NULL);
    texture(renderer->textures[3], GL_R8, GL_RED, frame.width,frame.height,NULL);
    gl->BindFramebuffer(GL_FRAMEBUFFER, renderer->fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,renderer->textures[2],0);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,renderer->textures[3],0);
    GLenum buffers[] = {GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1}; gl->DrawBuffers(2, buffers);
    if (gl->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        pp_frame_destroy(&frame); free(readback); return error_set(error, PP_ERROR_RENDER, "Incomplete frame framebuffer");
    }
    gl->ActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,renderer->textures[1]);
    gl->UseProgram(renderer->program);
#define LOCATION(name) gl->GetUniformLocation(renderer->program, name)
    gl->Uniform1i(LOCATION("image"),0); gl->Uniform1i(LOCATION("alpha"),1);
    gl->Uniform2f(LOCATION("destination_size"),(float)w,(float)h);
    gl->Uniform2f(LOCATION("source_size"),(float)source->width,(float)source->height);
    gl->Uniform2f(LOCATION("rotation"),(float)c,(float)s);
    gl->Uniform1f(LOCATION("scale"),(float)scale); gl->Uniform1f(LOCATION("opacity"),(float)opacity);
#undef LOCATION
    glDisable(GL_BLEND); glDisable(GL_DITHER); glViewport(0,0,frame.width,frame.height);
    gl->BindVertexArray(renderer->vao); glDrawArrays(GL_TRIANGLES,0,3);
    glReadBuffer(GL_COLOR_ATTACHMENT0); glReadPixels(0,0,frame.width,frame.height,GL_RGBA,GL_UNSIGNED_BYTE,readback);
    for (int y = 0; y < frame.height; y++) memcpy(frame.pixels+(size_t)y*frame.stride,readback+(size_t)(frame.height-1-y)*frame.stride,frame.stride);
    glReadBuffer(GL_COLOR_ATTACHMENT1); glReadPixels(0,0,frame.width,frame.height,GL_RED,GL_UNSIGNED_BYTE,readback);
    for (int y = 0; y < frame.height; y++) memcpy(frame.content_alpha+(size_t)y*frame.width,readback+(size_t)(frame.height-1-y)*frame.width,frame.width);
    free(readback);
    if (glGetError() != GL_NO_ERROR) { pp_frame_destroy(&frame); return error_set(error,PP_ERROR_RENDER,"OpenGL frame rendering failed"); }
    *out = frame; return true;
}
void pp_renderer_destroy(PPRenderer *renderer) {
    if (!renderer) return;
    if (renderer->context && SDL_GL_MakeCurrent(renderer->window, renderer->context)) {
        PPGL *gl = &renderer->gl;
        if (renderer->program) gl->DeleteProgram(renderer->program);
        if (renderer->vao) gl->DeleteVertexArrays(1,&renderer->vao);
        if (renderer->fbo) gl->DeleteFramebuffers(1,&renderer->fbo);
        glDeleteTextures(4,renderer->textures);
    }
    if (renderer->context) SDL_GL_DestroyContext(renderer->context);
    if (renderer->window) SDL_DestroyWindow(renderer->window);
    free(renderer); SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
