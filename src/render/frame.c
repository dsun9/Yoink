#include "gl_api.h"
#include "pointy/render.h"
#include "../core/geometry_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct PPRenderUniforms {GLint image,alpha,destination_size,source_size,rotation,scale,source_offset,opacity;} PPRenderUniforms;
struct PPRenderer {
    SDL_Window *window; SDL_GLContext context; PPGL gl;
    GLuint program, vao, fbo, textures[4];
    PPRenderUniforms uniforms,color_uniforms;
    GLuint color_program;
    PFNGLDEBUGMESSAGECALLBACKPROC error_callback;
    bool draw_error;
    uint32_t window_id;
    bool owns_window,borrows_context;
    GLuint preview_program,preview_texture;
    int source_width, source_height;
    uint64_t content_id, revision;
    PPRendererMetrics metrics;
    PPRenderer *group;
    PPSharedTexture *shared;
    GLuint output_texture;
    int output_width,output_height;
};
struct PPSharedTexture {PPRenderer *owner;GLuint name;int width,height;};
static void APIENTRY observe_error(GLenum source,GLenum type,GLuint id,GLenum severity,
    GLsizei length,const GLchar *message,const void *owner) {
    (void)source;(void)id;(void)severity;(void)length;(void)message;
    if(type==GL_DEBUG_TYPE_ERROR)((PPRenderer *)owner)->draw_error=true;
}
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
    "uniform vec2 rotation; uniform vec2 scale; uniform vec2 source_offset; uniform float opacity;"
    "out vec4 color; out vec4 coverage;"
    "void main() {"
    "vec2 d=vec2(gl_FragCoord.x,destination_size.y-gl_FragCoord.y)-destination_size*0.5;"
    "vec2 s=vec2(rotation.x*d.x+rotation.y*d.y,-rotation.y*d.x+rotation.x*d.y)/scale;"
    "vec2 uv=(s-source_offset)/source_size+0.5;"
    "if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1)))) { color=vec4(0); coverage=vec4(0); }"
    "else { color=texture(image,uv)*opacity; coverage=vec4(texture(alpha,uv).r,0,0,1); } }";
static const char *color_shader =
    "#version 150\n"
    "uniform sampler2D image; uniform vec2 destination_size; uniform vec2 source_size;"
    "uniform vec2 rotation; uniform vec2 scale; uniform vec2 source_offset; uniform float opacity;"
    "out vec4 color; void main() {"
    "vec2 d=vec2(gl_FragCoord.x,destination_size.y-gl_FragCoord.y)-destination_size*0.5;"
    "vec2 s=vec2(rotation.x*d.x+rotation.y*d.y,-rotation.y*d.x+rotation.x*d.y)/scale;"
    "vec2 uv=(s-source_offset)/source_size+0.5;"
    "color=(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))))?vec4(0):texture(image,uv)*opacity; }";
static const char *preview_shader =
    "#version 150\n"
    "uniform sampler2D image; uniform vec2 destination_size; uniform vec2 source_size;"
    "uniform vec2 rotation; uniform vec2 scale; uniform vec2 source_offset; uniform float opacity;"
    "out vec4 color; void main() {"
    "vec2 d=gl_FragCoord.xy-destination_size*0.5;"
    "vec2 s=vec2(rotation.x*d.x+rotation.y*d.y,-rotation.y*d.x+rotation.x*d.y)/scale;"
    "vec2 uv=(s-source_offset)/source_size+0.5;"
    "if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1)))) color=vec4(0);"
    "else color=texture(image,uv)*opacity; }";
typedef struct GLState {
    GLint program,vao,draw_fbo,read_fbo,draw_buffer,read_buffer,viewport[4];
    GLint active,texture0,active_texture,unpack_alignment,unpack_rows;
    GLboolean blend,dither,scissor,cull,depth;
} GLState;
static void save_state(PPGL *gl,GLState *state) {
    glGetIntegerv(GL_CURRENT_PROGRAM,&state->program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&state->vao);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&state->draw_fbo);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&state->read_fbo);
    glGetIntegerv(GL_DRAW_BUFFER,&state->draw_buffer);glGetIntegerv(GL_READ_BUFFER,&state->read_buffer);
    glGetIntegerv(GL_VIEWPORT,state->viewport);glGetIntegerv(GL_ACTIVE_TEXTURE,&state->active);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&state->active_texture);gl->ActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&state->texture0);gl->ActiveTexture(state->active);
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&state->unpack_alignment);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&state->unpack_rows);
    state->blend=glIsEnabled(GL_BLEND);state->dither=glIsEnabled(GL_DITHER);state->scissor=glIsEnabled(GL_SCISSOR_TEST);
    state->cull=glIsEnabled(GL_CULL_FACE);state->depth=glIsEnabled(GL_DEPTH_TEST);
}
static void enabled(GLenum setting,GLboolean value){if(value)glEnable(setting);else glDisable(setting);}
static void restore_state(PPGL *gl,const GLState *state) {
    gl->UseProgram(state->program);gl->BindVertexArray(state->vao);
    gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER,state->draw_fbo);glDrawBuffer(state->draw_buffer);
    gl->BindFramebuffer(GL_READ_FRAMEBUFFER,state->read_fbo);glReadBuffer(state->read_buffer);
    glViewport(state->viewport[0],state->viewport[1],state->viewport[2],state->viewport[3]);
    gl->ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,state->texture0);
    gl->ActiveTexture(state->active);glBindTexture(GL_TEXTURE_2D,state->active_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT,state->unpack_alignment);glPixelStorei(GL_UNPACK_ROW_LENGTH,state->unpack_rows);
    enabled(GL_BLEND,state->blend);enabled(GL_DITHER,state->dither);enabled(GL_SCISSOR_TEST,state->scissor);
    enabled(GL_CULL_FACE,state->cull);enabled(GL_DEPTH_TEST,state->depth);
}
static bool restore_context(SDL_Window *window,SDL_GLContext context,PPError *error) {
    return SDL_GL_MakeCurrent(window,context)||error_set(error,PP_ERROR_RENDER,"Cannot restore caller graphics context");
}
bool pp_renderer_create_for_ui(PPRenderer **out,uint32_t id,void *context,PPError *error) {
    if(out)*out=NULL;SDL_Window *window=SDL_GetWindowFromID(id);
    if(!out||!window||!context)return error_set(error,PP_ERROR_ARGUMENT,"Missing UI context or renderer output");
    SDL_Window *previous_window=SDL_GL_GetCurrentWindow();SDL_GLContext previous_context=SDL_GL_GetCurrentContext();
    if(!SDL_GL_MakeCurrent(window,context))return error_set(error,PP_ERROR_RENDER,SDL_GetError());
    PPRenderer *r=calloc(1,sizeof(*r));if(!r){restore_context(previous_window,previous_context,NULL);return error_set(error,PP_ERROR_MEMORY,"Cannot allocate UI renderer");}
    r->window=window;r->window_id=id;r->context=context;r->borrows_context=true;r->group=r;
    bool ok=pp_gl_load(&r->gl,error),saved=ok;GLState state={0};if(saved)save_state(&r->gl,&state);
    if(ok){r->preview_program=pp_gl_program(&r->gl,vertex_shader,preview_shader,error);ok=r->preview_program!=0;}
    if(ok){r->gl.GenVertexArrays(1,&r->vao);r->gl.GenFramebuffers(1,&r->fbo);glGenTextures(4,r->textures);ok=glGetError()==GL_NO_ERROR;}
    PPGL gl=r->gl;
    if(!ok){if(saved)pp_renderer_destroy(r);else free(r);}
    if(saved)restore_state(&gl,&state);
    if(!restore_context(previous_window,previous_context,error)) {if(ok)pp_renderer_destroy(r);return false;}
    if(!ok)return false;*out=r;return true;
}
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
    int previous_flags=0;SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS,&previous_flags);
#ifdef _WIN32
    if(window_id)SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,previous_flags|SDL_GL_CONTEXT_DEBUG_FLAG);
#endif
    if (renderer->window) renderer->context = SDL_GL_CreateContext(renderer->window);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,previous_flags);
#ifdef _WIN32
    if(!renderer->context && window_id)renderer->context=SDL_GL_CreateContext(renderer->window);
#endif
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,previous_share);
    if (!renderer->context || !SDL_GL_MakeCurrent(renderer->window, renderer->context)) {
        error_set(error, PP_ERROR_RENDER, SDL_GetError()); pp_renderer_destroy(renderer); return false;
    }
    /* Native event delivery and the runtime scheduler pace interaction. A
     * per-sprite swap must not wait for another refresh inside that owner work.
     * Unsupported contexts retain their driver policy and remain measurable. */
    if(window_id)SDL_GL_SetSwapInterval(0);
    if (!pp_gl_load(&renderer->gl, error)) { pp_renderer_destroy(renderer); return false; }
    PPGL *gl = &renderer->gl;
    renderer->program = pp_gl_program(gl, vertex_shader, window_id?color_shader:fragment_shader, error);
    if (!renderer->program) { pp_renderer_destroy(renderer); return false; }
#define CACHE(name) renderer->uniforms.name=gl->GetUniformLocation(renderer->program,#name)
    CACHE(image);CACHE(alpha);CACHE(destination_size);CACHE(source_size);
    CACHE(rotation);CACHE(scale);CACHE(source_offset);CACHE(opacity);
#undef CACHE
    gl->GenVertexArrays(1, &renderer->vao); gl->GenFramebuffers(1, &renderer->fbo);
    glGenTextures(4, renderer->textures);
    if (glGetError() != GL_NO_ERROR) { error_set(error,PP_ERROR_RENDER,"Cannot allocate GL frame resources"); pp_renderer_destroy(renderer); return false; }
#ifdef _WIN32
    if(window_id && SDL_GL_ExtensionSupported("GL_KHR_debug")) {
        GLint flags=0;glGetIntegerv(GL_CONTEXT_FLAGS,&flags);
        PFNGLDEBUGMESSAGECALLBACKPROC callback=(PFNGLDEBUGMESSAGECALLBACKPROC)SDL_GL_GetProcAddress("glDebugMessageCallback");
        PFNGLDEBUGMESSAGECONTROLPROC control=(PFNGLDEBUGMESSAGECONTROLPROC)SDL_GL_GetProcAddress("glDebugMessageControl");
        if((flags&GL_CONTEXT_FLAG_DEBUG_BIT)&&callback&&control) {
            glEnable(GL_DEBUG_OUTPUT);glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            control(GL_DONT_CARE,GL_DEBUG_TYPE_ERROR,GL_DONT_CARE,0,NULL,GL_TRUE);
            callback(observe_error,renderer);
            if(glGetError()==GL_NO_ERROR)renderer->error_callback=callback;
            else callback(NULL,NULL);
        }
    }
#endif
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
    renderer->draw_error=false; /* The explicit transactional check consumed it. */
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
    return pp_renderer_draw_source(renderer,layout,(PPRect){0,0,layout.x,layout.y},longest,angle_deg,opacity,density,error);
}
static void source_mapping(PPGL *gl,const PPRenderUniforms *u,int width,int height,
    int source_width,int source_height,PPVec2 layout,PPRect rect,double longest,
    double angle,double opacity,double density) {
    double radians=fmod(angle,360)*3.14159265358979323846/180;
    double scale=longest*density/fmax(layout.x,layout.y);
    gl->Uniform1i(u->image,0);gl->Uniform2f(u->destination_size,(float)width,(float)height);
    gl->Uniform2f(u->source_size,(float)source_width,(float)source_height);
    gl->Uniform2f(u->rotation,(float)cos(radians),(float)sin(radians));
    gl->Uniform2f(u->scale,(float)(scale*rect.w/source_width),(float)(scale*rect.h/source_height));
    gl->Uniform2f(u->source_offset,(float)((rect.x+rect.w*.5-layout.x*.5)*source_width/rect.w),
        (float)((rect.y+rect.h*.5-layout.y*.5)*source_height/rect.h));
    gl->Uniform1f(u->opacity,(float)opacity);
}
bool pp_renderer_preview_source(PPRenderer *r,const PPFrame *source,uint64_t id,uint64_t revision,
    PPVec2 layout,PPRect rect,double longest,double angle,double opacity,double density,
    unsigned *out,int *width,int *height,PPError *error) {
    int w,h;
    if(!r||!r->borrows_context||!out||!width||!height||!source||!source->pixels||
        !isfinite(opacity)||opacity<0||opacity>1||
        !pp_geometry_source_extent(layout,rect,longest,angle,density,&w,&h)||w>8192||h>8192)
        return error_set(error,PP_ERROR_ARGUMENT,"Invalid retained UI transform");
    SDL_Window *previous_window=SDL_GL_GetCurrentWindow();SDL_GLContext previous_context=SDL_GL_GetCurrentContext();
    if(previous_window!=r->window||previous_context!=r->context)
        return error_set(error,PP_ERROR_ARGUMENT,"UI preview requires its owning current context");
    if(!current(r,error)) {restore_context(previous_window,previous_context,NULL);return false;}
    GLState state;PPGL *gl=&r->gl;save_state(gl,&state);
    bool ok=pp_renderer_upload_source(r,source,id,revision,error);GLuint candidate=0;
    if(ok) {
        gl->ActiveTexture(GL_TEXTURE0);glGenTextures(1,&candidate);
        texture(candidate,GL_RGBA8,GL_RGBA,w,h,NULL);
        gl->BindFramebuffer(GL_FRAMEBUFFER,r->fbo);
        gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,candidate,0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        if(gl->CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
            ok=error_set(error,PP_ERROR_RENDER,"Incomplete UI preview framebuffer");
    }
    if(ok) {
        glBindTexture(GL_TEXTURE_2D,r->textures[0]);gl->UseProgram(r->preview_program);
        PPRenderUniforms uniforms;
#define LOCATION(name) uniforms.name=gl->GetUniformLocation(r->preview_program,#name)
        LOCATION(image);LOCATION(destination_size);LOCATION(source_size);LOCATION(rotation);
        LOCATION(scale);LOCATION(source_offset);LOCATION(opacity);
#undef LOCATION
        source_mapping(gl,&uniforms,w,h,source->width,source->height,layout,rect,longest,angle,opacity,density);
        glDisable(GL_BLEND);glDisable(GL_DITHER);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);
        glViewport(0,0,w,h);gl->BindVertexArray(r->vao);glDrawArrays(GL_TRIANGLES,0,3);
        GLenum failure=glGetError();
        if(failure!=GL_NO_ERROR)ok=error_set(error,failure==GL_OUT_OF_MEMORY?PP_ERROR_MEMORY:PP_ERROR_RENDER,"Retained UI drawing failed");
    }
    gl->BindFramebuffer(GL_FRAMEBUFFER,r->fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,0,0);
    if(!ok&&candidate){glDeleteTextures(1,&candidate);candidate=0;}
    restore_state(gl,&state);
    if(!restore_context(previous_window,previous_context,error)) {
        /* No output commit has happened. Delete only in the owning namespace,
         * then retry restoring the caller without replacing the original error. */
        if(SDL_GL_GetCurrentContext()==r->context||SDL_GL_MakeCurrent(r->window,r->context)) {
            if(candidate)glDeleteTextures(1,&candidate);restore_state(gl,&state);
        }
        restore_context(previous_window,previous_context,NULL);return false;
    }
    if(ok) {
        glDeleteTextures(1,&r->preview_texture);r->preview_texture=candidate;
        *out=candidate;*width=w;*height=h;r->metrics.transform_draws++;
    }
    return ok;
}
size_t pp_renderer_output_candidate_bytes(const PPRenderer *r,int width,int height) {
    if(!r||width<1||height<1||width>8192||height>8192)return 0;
    return r->output_texture&&r->output_width==width&&r->output_height==height?0:(size_t)width*height*4;
}
size_t pp_renderer_output_bytes(const PPRenderer *r) {
    return r&&r->output_texture?(size_t)r->output_width*r->output_height*4:0;
}
void pp_renderer_release_output(PPRenderer *r) {
    if(!r||!r->output_texture)return;
    /* A temporarily unavailable context keeps both its object and charge.
     * Renderer destruction ultimately releases the complete namespace. */
    if(!current(r,NULL))return;
    r->gl.BindFramebuffer(GL_FRAMEBUFFER,r->fbo);
    r->gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,0,0);
    glDeleteTextures(1,&r->output_texture);
    r->output_texture=0;r->output_width=r->output_height=0;
}
static bool publish_output(PPRenderer *r,int w,int h,PPError *error) {
    PPGL *gl=&r->gl;
    gl->BindFramebuffer(GL_READ_FRAMEBUFFER,r->fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER,0);glDrawBuffer(GL_BACK);
    glDisable(GL_SCISSOR_TEST);
    gl->BlitFramebuffer(0,0,w,h,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    if(r->error_callback?r->draw_error:glGetError()!=GL_NO_ERROR)
        return error_set(error,PP_ERROR_RENDER,"Retained OpenGL publication failed");
    return SDL_GL_SwapWindow(r->window)||error_set(error,PP_ERROR_RENDER,SDL_GetError());
}
bool pp_renderer_refresh(PPRenderer *r,PPError *error) {
    if(!r||!r->output_texture)return error_set(error,PP_ERROR_RENDER,"Missing retained presentation output");
    if(!current(r,error))return false;
    int w,h;if(!SDL_GetWindowSizeInPixels(r->window,&w,&h)||w!=r->output_width||h!=r->output_height)
        return error_set(error,PP_ERROR_RENDER,"Retained output does not match drawable bounds");
    r->gl.BindFramebuffer(GL_FRAMEBUFFER,r->fbo);
    r->gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r->output_texture,0);
    if(!publish_output(r,w,h,error))return false;
    r->metrics.presentation_refreshes++;return true;
}
bool pp_renderer_draw_source(PPRenderer *renderer,PPVec2 layout,PPRect rect,double longest,
    double angle_deg,double opacity,double density,PPError *error) {
    if(!renderer || renderer->owns_window || !renderer->content_id || !isfinite(layout.x) || !isfinite(layout.y) ||
        layout.x<=0 || layout.y<=0 || !isfinite(longest) || longest<=0 || !isfinite(angle_deg) ||
        !isfinite(opacity) || opacity<0 || opacity>1 || !isfinite(density) || density<=0)
        return error_set(error,PP_ERROR_ARGUMENT,"Invalid direct transform");
    int extent_w,extent_h;
    if(!pp_geometry_source_extent(layout,rect,longest,angle_deg,density,&extent_w,&extent_h))
        return error_set(error,PP_ERROR_ARGUMENT,"Direct transform exceeds drawable limits");
    if(!current(renderer,error)) return false;
    int w,h;if(!SDL_GetWindowSizeInPixels(renderer->window,&w,&h) || w!=(int)extent_w || h!=(int)extent_h)
        return error_set(error,PP_ERROR_ARGUMENT,"Direct window drawable does not match canonical transform bounds");
    PPGL *gl=&renderer->gl;GLuint candidate=0,output=renderer->output_texture;
    if(pp_renderer_output_candidate_bytes(renderer,w,h)) {
        glGenTextures(1,&candidate);texture(candidate,GL_RGBA8,GL_RGBA,w,h,NULL);output=candidate;
    }
    gl->BindFramebuffer(GL_FRAMEBUFFER,renderer->fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    bool ok=gl->CheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    if(!ok)error_set(error,PP_ERROR_RENDER,"Incomplete retained native framebuffer");
    gl->ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,renderer->shared?renderer->shared->name:renderer->textures[0]);
    gl->UseProgram(renderer->program);
    source_mapping(gl,&renderer->uniforms,w,h,renderer->source_width,renderer->source_height,
        layout,rect,longest,angle_deg,opacity,density);
    glDisable(GL_BLEND);glDisable(GL_DITHER);glDisable(GL_SCISSOR_TEST);glViewport(0,0,w,h);
    if(ok) {
        gl->BindVertexArray(renderer->vao);glDrawArrays(GL_TRIANGLES,0,3);
        if(renderer->error_callback?renderer->draw_error:glGetError()!=GL_NO_ERROR)
            ok=error_set(error,PP_ERROR_RENDER,"Direct OpenGL rendering failed");
    }
    if(ok)ok=publish_output(renderer,w,h,error);
    if(!ok) {
        gl->BindFramebuffer(GL_FRAMEBUFFER,renderer->fbo);
        gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,renderer->output_texture,0);
        if(candidate)glDeleteTextures(1,&candidate);return false;
    }
    if(candidate){glDeleteTextures(1,&renderer->output_texture);renderer->output_texture=candidate;}
    renderer->output_width=w;renderer->output_height=h;
    renderer->metrics.transform_draws++;return true;
}
bool pp_renderer_transform(PPRenderer *renderer, const PPFrame *source, double longest_dip,
    double angle_deg, double opacity, double pixels_per_dip, PPFrame *out, PPError *error) {
    if(!source)return error_set(error,PP_ERROR_ARGUMENT,"Missing transform source");
    PPVec2 layout={source->width,source->height};PPRect rect={0,0,source->width,source->height};
    return pp_renderer_transform_source(renderer,source,layout,rect,longest_dip,angle_deg,opacity,pixels_per_dip,out,error);
}
static bool transform_source(PPRenderer *renderer,const PPFrame *source,PPVec2 layout,PPRect rect,
    double longest_dip,double angle_deg,double opacity,double pixels_per_dip,bool coverage,PPFrame *out,PPError *error) {
    if (!renderer || !renderer->owns_window || !source || !source->pixels || (coverage&&!source->content_alpha) || !out || out->pixels || out->content_alpha ||
        source->width <= 0 || source->height <= 0 || source->width > 16384 || source->height > 16384 ||
        source->stride < (size_t)source->width*4 || source->stride % 4 || source->stride/4 > 2147483647 ||
        !isfinite(longest_dip) || longest_dip <= 0 || !isfinite(angle_deg) || !isfinite(opacity) || opacity < 0 || opacity > 1 ||
        !isfinite(pixels_per_dip) || pixels_per_dip <= 0)
        return error_set(error, PP_ERROR_ARGUMENT, "Invalid frame transform");
    int drawable_w,drawable_h;
    if (!pp_geometry_source_extent(layout,rect,longest_dip,angle_deg,pixels_per_dip,&drawable_w,&drawable_h))
        return error_set(error, PP_ERROR_ARGUMENT, "Transformed frame dimensions exceed limits");
    double w=drawable_w,h=drawable_h;
    PPFrame frame = {0}; frame.width = (int)w; frame.height = (int)h; frame.stride = (size_t)frame.width*4;
    size_t count = (size_t)frame.width*frame.height;
    frame.pixels = malloc(count*4); if(coverage)frame.content_alpha = malloc(count);
    uint8_t *readback = malloc(count*4);
    if (!frame.pixels || (coverage&&!frame.content_alpha) || !readback) {
        pp_frame_destroy(&frame); free(readback); return error_set(error, PP_ERROR_MEMORY, "Cannot allocate transformed pixels");
    }
    if (!current(renderer,error)) {
        pp_frame_destroy(&frame); free(readback); return error_set(error, PP_ERROR_RENDER, SDL_GetError());
    }
    PPGL *gl = &renderer->gl;
    if(!coverage&&!renderer->color_program) {
        renderer->color_program=pp_gl_program(gl,vertex_shader,color_shader,error);
        if(!renderer->color_program){pp_frame_destroy(&frame);free(readback);return false;}
#define CACHE(name) renderer->color_uniforms.name=gl->GetUniformLocation(renderer->color_program,#name)
        CACHE(image);CACHE(destination_size);CACHE(source_size);CACHE(rotation);CACHE(scale);CACHE(source_offset);CACHE(opacity);
#undef CACHE
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl->ActiveTexture(GL_TEXTURE0); glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(source->stride/4));
    texture(renderer->textures[0], GL_RGBA8, GL_RGBA, source->width,source->height,source->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); gl->ActiveTexture(GL_TEXTURE1);
    if(coverage)texture(renderer->textures[1], GL_R8, GL_RED, source->width,source->height,source->content_alpha);
    texture(renderer->textures[2], GL_RGBA8, GL_RGBA, frame.width,frame.height,NULL);
    if(coverage)texture(renderer->textures[3], GL_R8, GL_RED, frame.width,frame.height,NULL);
    gl->BindFramebuffer(GL_FRAMEBUFFER, renderer->fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,renderer->textures[2],0);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,coverage?renderer->textures[3]:0,0);
    GLenum buffers[] = {GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1}; gl->DrawBuffers(coverage?2:1, buffers);
    if (gl->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        pp_frame_destroy(&frame); free(readback); return error_set(error, PP_ERROR_RENDER, "Incomplete frame framebuffer");
    }
    if(coverage){gl->ActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,renderer->textures[1]);}
    gl->UseProgram(coverage?renderer->program:renderer->color_program);
    const PPRenderUniforms *uniforms=coverage?&renderer->uniforms:&renderer->color_uniforms;
    source_mapping(gl,uniforms,frame.width,frame.height,source->width,source->height,
        layout,rect,longest_dip,angle_deg,opacity,pixels_per_dip);
    if(coverage)gl->Uniform1i(uniforms->alpha,1);
    glDisable(GL_BLEND); glDisable(GL_DITHER); glViewport(0,0,frame.width,frame.height);
    gl->BindVertexArray(renderer->vao); glDrawArrays(GL_TRIANGLES,0,3);
    glReadBuffer(GL_COLOR_ATTACHMENT0); glReadPixels(0,0,frame.width,frame.height,GL_RGBA,GL_UNSIGNED_BYTE,readback);
    for (int y = 0; y < frame.height; y++) memcpy(frame.pixels+(size_t)y*frame.stride,readback+(size_t)(frame.height-1-y)*frame.stride,frame.stride);
    if(coverage){glReadBuffer(GL_COLOR_ATTACHMENT1); glReadPixels(0,0,frame.width,frame.height,GL_RED,GL_UNSIGNED_BYTE,readback);
      for (int y = 0; y < frame.height; y++) memcpy(frame.content_alpha+(size_t)y*frame.width,readback+(size_t)(frame.height-1-y)*frame.width,frame.width);}
    free(readback);
    if (glGetError() != GL_NO_ERROR) { pp_frame_destroy(&frame); return error_set(error,PP_ERROR_RENDER,"OpenGL frame rendering failed"); }
    renderer->metrics.source_uploads++;renderer->metrics.transform_draws++;renderer->metrics.readbacks+=coverage?2:1;
    *out = frame; return true;
}
bool pp_renderer_transform_source(PPRenderer *renderer,const PPFrame *source,PPVec2 layout,PPRect rect,
    double longest,double angle,double opacity,double density,PPFrame *out,PPError *error) {
    return transform_source(renderer,source,layout,rect,longest,angle,opacity,density,true,out,error);
}
bool pp_renderer_transform_color_source(PPRenderer *renderer,const PPFrame *source,PPVec2 layout,PPRect rect,
    double longest,double angle,double opacity,double density,PPFrame *out,PPError *error) {
    return transform_source(renderer,source,layout,rect,longest,angle,opacity,density,false,out,error);
}
void pp_renderer_destroy(PPRenderer *renderer) {
    if (!renderer) return;
    SDL_Window *previous_window=SDL_GL_GetCurrentWindow();SDL_GLContext previous_context=SDL_GL_GetCurrentContext();
    GLState state={0};bool saved=false;
    SDL_Window *window=SDL_GetWindowFromID(renderer->window_id);
    if (window && renderer->context && SDL_GL_MakeCurrent(window, renderer->context)) {
        if(renderer->borrows_context){save_state(&renderer->gl,&state);saved=true;}
        if(renderer->error_callback)renderer->error_callback(NULL,NULL);
        if(renderer->shared) {glFinish();glBindTexture(GL_TEXTURE_2D,0);renderer->shared=NULL;}
        PPGL *gl = &renderer->gl;
        if (renderer->program) gl->DeleteProgram(renderer->program);
        if (renderer->preview_program) gl->DeleteProgram(renderer->preview_program);
        if (renderer->color_program) gl->DeleteProgram(renderer->color_program);
        if (renderer->vao) gl->DeleteVertexArrays(1,&renderer->vao);
        if (renderer->fbo) gl->DeleteFramebuffers(1,&renderer->fbo);
        glDeleteTextures(4,renderer->textures);
        glDeleteTextures(1,&renderer->preview_texture);
        glDeleteTextures(1,&renderer->output_texture);
        if(saved) {
            for(unsigned i=0;i<4;i++) {
                if(state.texture0==(GLint)renderer->textures[i])state.texture0=0;
                if(state.active_texture==(GLint)renderer->textures[i])state.active_texture=0;
            }
            if(state.texture0==(GLint)renderer->preview_texture)state.texture0=0;
            if(state.active_texture==(GLint)renderer->preview_texture)state.active_texture=0;
            restore_state(&renderer->gl,&state);
        }
    }
    if (renderer->context&&!renderer->borrows_context) SDL_GL_DestroyContext(renderer->context);
    if (window && renderer->owns_window) SDL_DestroyWindow(window);
    bool owns_window=renderer->owns_window;
    bool borrowed=renderer->borrows_context;
    free(renderer); if(owns_window) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    if(borrowed)restore_context(previous_window,previous_context,NULL);
}
