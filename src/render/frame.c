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
    uint32_t window_id;
    bool owns_window;
    int source_width, source_height;
    uint64_t content_id, revision;
    PPRendererMetrics metrics;
    PPRenderer *group;
    PPSharedTexture *shared;
};
struct PPSharedTexture {PPRenderer *owner;GLuint name;int width,height;};
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
    "uniform vec2 rotation; uniform vec2 scale; uniform float opacity;"
    "out vec4 color; out vec4 coverage;"
    "void main() {"
    "vec2 d=vec2(gl_FragCoord.x,destination_size.y-gl_FragCoord.y)-destination_size*0.5;"
    "vec2 s=vec2(rotation.x*d.x+rotation.y*d.y,-rotation.y*d.x+rotation.x*d.y)/scale;"
    "vec2 uv=s/source_size+0.5;"
    "if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1)))) { color=vec4(0); coverage=vec4(0); }"
    "else { color=texture(image,uv)*opacity; coverage=vec4(texture(alpha,uv).r,0,0,1); } }";
static bool create(PPRenderer **out, uint32_t window_id,PPRenderer *share,bool texture_owner, PPError *error) {
    if (!out) return error_set(error, PP_ERROR_ARGUMENT, "Missing renderer output");
    *out = NULL;
    if (!window_id && !SDL_InitSubSystem(SDL_INIT_VIDEO)) return error_set(error, PP_ERROR_RENDER, SDL_GetError());
    PPRenderer *renderer = calloc(1, sizeof(*renderer));
    if (!renderer) { if(!window_id) SDL_QuitSubSystem(SDL_INIT_VIDEO); return error_set(error, PP_ERROR_MEMORY, "Cannot allocate renderer"); }
    renderer->owns_window=!window_id;
    renderer->group=share?share:renderer;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    if(!window_id) {
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, texture_owner?1:0);
        if(texture_owner) {SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,8);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);}
        renderer->window = SDL_CreateWindow("PointyPointer renderer", 1, 1, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    } else renderer->window=SDL_GetWindowFromID(window_id);
    if(renderer->window) renderer->window_id=SDL_GetWindowID(renderer->window);
    int previous_share=0;SDL_GL_GetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,&previous_share);
    if(share && !SDL_GL_MakeCurrent(share->window,share->context)) {
        error_set(error,PP_ERROR_RENDER,SDL_GetError());pp_renderer_destroy(renderer);return false;
    }
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,share?1:0);
    if (renderer->window) renderer->context = SDL_GL_CreateContext(renderer->window);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,previous_share);
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
bool pp_renderer_create(PPRenderer **out, PPError *error) {return create(out,0,NULL,false,error);}
bool pp_renderer_create_texture_owner(PPRenderer **out,PPError *error) {return create(out,0,NULL,true,error);}
bool pp_renderer_create_shared_window(PPRenderer **out,uint32_t id,PPRenderer *owner,PPError *error) {
    if(!owner || owner->group!=owner || !id) return error_set(error,PP_ERROR_ARGUMENT,"Missing texture share group");
    return create(out,id,owner,false,error);
}
bool pp_renderer_create_for_window(PPRenderer **out, uint32_t id, PPError *error) {
    if(out) *out=NULL;
    if(!id || !SDL_GetWindowFromID(id)) return error_set(error,PP_ERROR_ARGUMENT,"Missing direct OpenGL window");
    return create(out,id,NULL,false,error);
}
PPRendererMetrics pp_renderer_metrics(const PPRenderer *renderer) {
    return renderer?renderer->metrics:(PPRendererMetrics){0};
}
static bool current(PPRenderer *renderer,PPError *error) {
    SDL_Window *window=renderer?SDL_GetWindowFromID(renderer->window_id):NULL;
    if(!window || !renderer->context || !SDL_GL_MakeCurrent(window,renderer->context))
        return error_set(error,PP_ERROR_RENDER,"Renderer window or OpenGL context is unavailable");
    return true;
}
bool pp_renderer_complete(PPRenderer *renderer,PPError *error) {
    if(!current(renderer,error)) return false;
    glFinish();
    return glGetError()==GL_NO_ERROR || error_set(error,PP_ERROR_RENDER,"OpenGL completion failed");
}
static void texture(GLuint texture, GLint format, GLenum data_format, int w, int h, const void *data) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, data_format, GL_UNSIGNED_BYTE, data);
}
bool pp_shared_texture_create(PPRenderer *owner,const PPFrame *frame,PPSharedTexture **out,PPError *error) {
    if(out)*out=NULL;
    if(!out || !owner || owner->group!=owner || !frame || !frame->pixels || frame->width<=0 || frame->height<=0 ||
       frame->width>8192 || frame->height>8192 || frame->stride<(size_t)frame->width*4 || frame->stride%4)
        return error_set(error,PP_ERROR_ARGUMENT,"Invalid immutable shared frame");
    PPSharedTexture *candidate=calloc(1,sizeof(*candidate));
    if(!candidate)return error_set(error,PP_ERROR_MEMORY,"Cannot allocate shared frame owner");
    if(!current(owner,error)){free(candidate);return false;}
    glGenTextures(1,&candidate->name);owner->gl.ActiveTexture(GL_TEXTURE0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,(GLint)(frame->stride/4));
    texture(candidate->name,GL_RGBA8,GL_RGBA,frame->width,frame->height,frame->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    /* Immutable publication waits once on a cache miss. Consumers subsequently
     * bind the completed object in their own context; no shared mutable frame. */
    glFinish();GLenum failure=glGetError();
    if(failure!=GL_NO_ERROR){glDeleteTextures(1,&candidate->name);free(candidate);return error_set(error,failure==GL_OUT_OF_MEMORY?PP_ERROR_MEMORY:PP_ERROR_RENDER,"Cannot publish shared frame texture");}
    candidate->owner=owner;candidate->width=frame->width;candidate->height=frame->height;
    owner->metrics.source_uploads++;*out=candidate;return true;
}
bool pp_renderer_bind_shared(PPRenderer *renderer,PPSharedTexture *source,uint64_t id,uint64_t revision,PPError *error) {
    if(!renderer || !source || renderer->group!=source->owner || !id || !revision)
        return error_set(error,PP_ERROR_ARGUMENT,"Texture belongs to another context group");
    if(renderer->shared==source && renderer->content_id==id && renderer->revision==revision)return true;
    if(!current(renderer,error))return false;
    /* Complete the previous binding before its final application reference can
     * be retired. All joined consumers use the same serialized owner thread. */
    if(renderer->content_id)glFinish();
    renderer->gl.ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source->name);
    if(glGetError()!=GL_NO_ERROR)return error_set(error,PP_ERROR_RENDER,"Cannot bind shared frame");
    if(!renderer->shared && renderer->content_id) {
        glDeleteTextures(1,&renderer->textures[0]);glGenTextures(1,&renderer->textures[0]);
    }
    renderer->shared=source;renderer->content_id=id;renderer->revision=revision;
    renderer->source_width=source->width;renderer->source_height=source->height;return true;
}
void pp_shared_texture_destroy(PPSharedTexture *source) {
    if(!source)return;
    if(current(source->owner,NULL))glDeleteTextures(1,&source->name);
    free(source);
}
bool pp_renderer_upload_source(PPRenderer *renderer,const PPFrame *source,
    uint64_t content_id,uint64_t revision,PPError *error) {
    if(!renderer || renderer->owns_window || !source || !source->pixels || !content_id ||
        source->width<=0 || source->height<=0 || source->width>16384 || source->height>16384 ||
        source->stride<(size_t)source->width*4 || source->stride%4 || source->stride/4>2147483647)
        return error_set(error,PP_ERROR_ARGUMENT,"Invalid direct source");
    if(renderer->content_id==content_id && renderer->revision==revision &&
        renderer->source_width==source->width && renderer->source_height==source->height) return true;
    if(!current(renderer,error)) return false;
    PPGL *gl=&renderer->gl; gl->ActiveTexture(GL_TEXTURE0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1); glPixelStorei(GL_UNPACK_ROW_LENGTH,(GLint)(source->stride/4));
    bool replacement=renderer->content_id!=content_id || renderer->source_width!=source->width || renderer->source_height!=source->height;
    GLuint candidate=0;
    if(!replacement) {
        glBindTexture(GL_TEXTURE_2D,renderer->textures[0]);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,source->width,source->height,GL_RGBA,GL_UNSIGNED_BYTE,source->pixels);
    } else {
        /* Keep the usable texture until a separately reserved candidate upload
         * succeeds. The presenter accounts the overlap before entering here. */
        glGenTextures(1,&candidate);
        texture(candidate,GL_RGBA8,GL_RGBA,source->width,source->height,source->pixels);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    GLenum upload_error=glGetError();
    if(upload_error!=GL_NO_ERROR) {
        if(candidate) glDeleteTextures(1,&candidate);
        return error_set(error,upload_error==GL_OUT_OF_MEMORY?PP_ERROR_MEMORY:PP_ERROR_RENDER,"Cannot upload direct source texture");
    }
    if(replacement) {glDeleteTextures(1,&renderer->textures[0]);renderer->textures[0]=candidate;}
    if(renderer->shared){glFinish();renderer->shared=NULL;}
    renderer->source_width=source->width;renderer->source_height=source->height;
    renderer->content_id=content_id;renderer->revision=revision;renderer->metrics.source_uploads++;
    return true;
}
bool pp_renderer_draw(PPRenderer *renderer,PPVec2 layout,double longest,
    double angle_deg,double opacity,double density,PPError *error) {
    if(!renderer || renderer->owns_window || !renderer->content_id || !isfinite(layout.x) || !isfinite(layout.y) ||
        layout.x<=0 || layout.y<=0 || !isfinite(longest) || longest<=0 || !isfinite(angle_deg) ||
        !isfinite(opacity) || opacity<0 || opacity>1 || !isfinite(density) || density<=0)
        return error_set(error,PP_ERROR_ARGUMENT,"Invalid direct transform");
    double scale=longest*density/fmax(layout.x,layout.y);
    double angle=fmod(angle_deg,360)*3.14159265358979323846/180,c=cos(angle),s=sin(angle);
    double extent_w=ceil(scale*(fabs(c)*layout.x+fabs(s)*layout.y)-1e-9);
    double extent_h=ceil(scale*(fabs(s)*layout.x+fabs(c)*layout.y)-1e-9);
    if(!isfinite(extent_w) || !isfinite(extent_h) || extent_w<1 || extent_h<1 || extent_w>16384 || extent_h>16384)
        return error_set(error,PP_ERROR_ARGUMENT,"Direct transform exceeds drawable limits");
    if(!current(renderer,error)) return false;
    int w,h;if(!SDL_GetWindowSizeInPixels(renderer->window,&w,&h) || w!=(int)extent_w || h!=(int)extent_h)
        return error_set(error,PP_ERROR_ARGUMENT,"Direct window drawable does not match canonical transform bounds");
    PPGL *gl=&renderer->gl;gl->BindFramebuffer(GL_FRAMEBUFFER,0);glDrawBuffer(GL_BACK);
    gl->ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,renderer->shared?renderer->shared->name:renderer->textures[0]);
    gl->UseProgram(renderer->program);
#define LOCATION(name) gl->GetUniformLocation(renderer->program,name)
    gl->Uniform1i(LOCATION("image"),0);gl->Uniform1i(LOCATION("alpha"),1);
    gl->Uniform2f(LOCATION("destination_size"),(float)w,(float)h);
    gl->Uniform2f(LOCATION("source_size"),(float)renderer->source_width,(float)renderer->source_height);
    gl->Uniform2f(LOCATION("rotation"),(float)c,(float)s);
    gl->Uniform2f(LOCATION("scale"),(float)(scale*layout.x/renderer->source_width),(float)(scale*layout.y/renderer->source_height));
    gl->Uniform1f(LOCATION("opacity"),(float)opacity);
#undef LOCATION
    glDisable(GL_BLEND);glDisable(GL_DITHER);glViewport(0,0,w,h);
    gl->BindVertexArray(renderer->vao);glDrawArrays(GL_TRIANGLES,0,3);
    if(glGetError()!=GL_NO_ERROR) return error_set(error,PP_ERROR_RENDER,"Direct OpenGL rendering failed");
    if(!SDL_GL_SwapWindow(renderer->window)) return error_set(error,PP_ERROR_RENDER,SDL_GetError());
    renderer->metrics.transform_draws++;return true;
}
bool pp_renderer_transform(PPRenderer *renderer, const PPFrame *source, double longest_dip,
    double angle_deg, double opacity, double pixels_per_dip, PPFrame *out, PPError *error) {
    if (!renderer || !renderer->owns_window || !source || !source->pixels || !source->content_alpha || !out || out->pixels || out->content_alpha ||
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
    if (!current(renderer,error)) {
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
    gl->Uniform2f(LOCATION("scale"),(float)scale,(float)scale); gl->Uniform1f(LOCATION("opacity"),(float)opacity);
#undef LOCATION
    glDisable(GL_BLEND); glDisable(GL_DITHER); glViewport(0,0,frame.width,frame.height);
    gl->BindVertexArray(renderer->vao); glDrawArrays(GL_TRIANGLES,0,3);
    glReadBuffer(GL_COLOR_ATTACHMENT0); glReadPixels(0,0,frame.width,frame.height,GL_RGBA,GL_UNSIGNED_BYTE,readback);
    for (int y = 0; y < frame.height; y++) memcpy(frame.pixels+(size_t)y*frame.stride,readback+(size_t)(frame.height-1-y)*frame.stride,frame.stride);
    glReadBuffer(GL_COLOR_ATTACHMENT1); glReadPixels(0,0,frame.width,frame.height,GL_RED,GL_UNSIGNED_BYTE,readback);
    for (int y = 0; y < frame.height; y++) memcpy(frame.content_alpha+(size_t)y*frame.width,readback+(size_t)(frame.height-1-y)*frame.width,frame.width);
    free(readback);
    if (glGetError() != GL_NO_ERROR) { pp_frame_destroy(&frame); return error_set(error,PP_ERROR_RENDER,"OpenGL frame rendering failed"); }
    renderer->metrics.source_uploads++;renderer->metrics.transform_draws++;renderer->metrics.readbacks+=2;
    *out = frame; return true;
}
void pp_renderer_destroy(PPRenderer *renderer) {
    if (!renderer) return;
    SDL_Window *window=SDL_GetWindowFromID(renderer->window_id);
    if (window && renderer->context && SDL_GL_MakeCurrent(window, renderer->context)) {
        if(renderer->shared) {glFinish();glBindTexture(GL_TEXTURE_2D,0);renderer->shared=NULL;}
        PPGL *gl = &renderer->gl;
        if (renderer->program) gl->DeleteProgram(renderer->program);
        if (renderer->vao) gl->DeleteVertexArrays(1,&renderer->vao);
        if (renderer->fbo) gl->DeleteFramebuffers(1,&renderer->fbo);
        glDeleteTextures(4,renderer->textures);
    }
    if (renderer->context) SDL_GL_DestroyContext(renderer->context);
    if (window && renderer->owns_window) SDL_DestroyWindow(window);
    bool owns_window=renderer->owns_window;
    free(renderer); if(owns_window) SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
