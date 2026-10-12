#include "performance_probe.h"
#include "../../src/render/gl_api.h"
static GLuint elapsed_query;
static SDL_GLContext query_context;
static PFNGLGENQUERIESPROC query_gen;
static PFNGLBEGINQUERYPROC query_begin;
static PFNGLENDQUERYPROC query_end;
static PFNGLGETQUERYOBJECTUI64VPROC query_result;
static PFNGLDELETEQUERIESPROC query_delete;
static bool measured_draw;
static void state_timing(const char *name,double start) {
  double ms=pp_probe_clock()-start;pp_probe_record(PROBE_DRAW_STATE,start);
  if(ms>50)printf("STATE slow_call=%s elapsed_ms=%.3f\n",name,ms);
}
#define STATE_FUNCTION(name,type,parameters,args) \
  static type state_raw_##name; \
  static void APIENTRY state_##name parameters {double start=pp_probe_clock();state_raw_##name args;state_timing(#name,start);}
STATE_FUNCTION(BindFramebuffer,PFNGLBINDFRAMEBUFFERPROC,(GLenum target,GLuint object),(target,object))
STATE_FUNCTION(ActiveTexture,PFNGLACTIVETEXTUREPROC,(GLenum texture),(texture))
STATE_FUNCTION(UseProgram,PFNGLUSEPROGRAMPROC,(GLuint program),(program))
STATE_FUNCTION(Uniform1i,PFNGLUNIFORM1IPROC,(GLint location,GLint value),(location,value))
STATE_FUNCTION(Uniform1f,PFNGLUNIFORM1FPROC,(GLint location,GLfloat value),(location,value))
STATE_FUNCTION(Uniform2f,PFNGLUNIFORM2FPROC,(GLint location,GLfloat x,GLfloat y),(location,x,y))
STATE_FUNCTION(BindVertexArray,PFNGLBINDVERTEXARRAYPROC,(GLuint object),(object))
#undef STATE_FUNCTION
static PFNGLGETUNIFORMLOCATIONPROC state_raw_location;
static GLint APIENTRY state_location(GLuint program,const GLchar *name) {
  double start=pp_probe_clock();GLint result=state_raw_location(program,name);state_timing("GetUniformLocation",start);return result;
}
static bool state_load(PPGL *gl,PPError *error) {
  if(!pp_gl_load(gl,error))return false;
#define WRAP(name) state_raw_##name=gl->name;gl->name=state_##name
  WRAP(BindFramebuffer);WRAP(ActiveTexture);WRAP(UseProgram);WRAP(Uniform1i);WRAP(Uniform1f);WRAP(Uniform2f);WRAP(BindVertexArray);
#undef WRAP
  state_raw_location=gl->GetUniformLocation;gl->GetUniformLocation=state_location;return true;
}
static void APIENTRY state_draw_buffer(GLenum mode) {double start=pp_probe_clock();glDrawBuffer(mode);state_timing("DrawBuffer",start);}
static void APIENTRY state_bind_texture(GLenum target,GLuint texture) {double start=pp_probe_clock();glBindTexture(target,texture);state_timing("BindTexture",start);}
static void APIENTRY state_disable(GLenum cap) {double start=pp_probe_clock();glDisable(cap);state_timing("Disable",start);}
static void APIENTRY state_viewport(GLint x,GLint y,GLsizei w,GLsizei h) {double start=pp_probe_clock();glViewport(x,y,w,h);state_timing("Viewport",start);}
static GLenum APIENTRY state_error(void) {double start=pp_probe_clock();GLenum result=glGetError();state_timing("GetError",start);return result;}
static bool state_size(SDL_Window *window,int *w,int *h) {double start=pp_probe_clock();bool result=SDL_GetWindowSizeInPixels(window,w,h);state_timing("GetWindowSizeInPixels",start);return result;}
static void APIENTRY probe_draw(GLenum mode, GLint first, GLsizei count) {
  /* Borrowed UI FBO draws belong to their own UI observation. Only the
   * wrapped native/export operations below have a completion boundary and
   * may enter this diagnostic timer-query scope. */
  if(!measured_draw){glDrawArrays(mode,first,count);return;}
  if(pp_probe_completion_enabled && SDL_GL_ExtensionSupported("GL_ARB_timer_query")) {
    query_gen=(PFNGLGENQUERIESPROC)SDL_GL_GetProcAddress("glGenQueries");
    query_begin=(PFNGLBEGINQUERYPROC)SDL_GL_GetProcAddress("glBeginQuery");
    query_end=(PFNGLENDQUERYPROC)SDL_GL_GetProcAddress("glEndQuery");
    query_result=(PFNGLGETQUERYOBJECTUI64VPROC)SDL_GL_GetProcAddress("glGetQueryObjectui64v");
    query_delete=(PFNGLDELETEQUERIESPROC)SDL_GL_GetProcAddress("glDeleteQueries");
    if(query_gen && query_begin && query_end && query_result && query_delete) {
      query_gen(1,&elapsed_query);
      query_context=SDL_GL_GetCurrentContext();
      query_begin(GL_TIME_ELAPSED,elapsed_query);
    }
  }
  double start=pp_probe_clock(); glDrawArrays(mode,first,count); pp_probe_record(PROBE_GPU_SUBMIT,start);
  if(elapsed_query) query_end(GL_TIME_ELAPSED);
}
static void query_measurement(void) {
  /* Query names belong to the draw context. Each draw allocates, resolves and
   * deletes its query before another retained sprite context becomes current. */
  if(elapsed_query && query_context==SDL_GL_GetCurrentContext()) {
    GLuint64 ns=0;double start=pp_probe_clock();query_result(elapsed_query,GL_QUERY_RESULT,&ns);
    pp_probe_record(PROBE_QUERY_RESULT,start);
    double ms=ns/1000000.0; PPProbeMetric *m=&pp_probe_metrics[PROBE_GPU_EXECUTION];
    m->count++; m->ms+=ms; if(ms>m->max_ms) m->max_ms=ms;
    query_delete(1,&elapsed_query); elapsed_query=0;query_context=NULL;
  }
}
static bool probe_current(SDL_Window *window,SDL_GLContext context) {
  double start=pp_probe_clock();bool ok=SDL_GL_MakeCurrent(window,context);pp_probe_record(PROBE_CONTEXT,start);return ok;
}
static bool probe_swap(SDL_Window *window) {
  double start=pp_probe_clock();bool ok=SDL_GL_SwapWindow(window);pp_probe_record(PROBE_GL_SWAP,start);return ok;
}
static void APIENTRY probe_finish(void) {
  double start=pp_probe_clock();glFinish();pp_probe_record(PROBE_GL_FINISH,start);
}
static void discard_query(void) {
  if(elapsed_query && query_context==SDL_GL_GetCurrentContext()) query_delete(1,&elapsed_query);
  elapsed_query=0;query_context=NULL;
}
static void APIENTRY probe_read(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels) {
  double start=pp_probe_clock(); glReadPixels(x,y,w,h,format,type,pixels); pp_probe_record(PROBE_READBACK,start);
}
static void APIENTRY probe_texture(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *pixels) {
  double start=pp_probe_clock(); glTexImage2D(target,level,internal,w,h,border,format,type,pixels); pp_probe_record(PROBE_TEXTURE,start);
}
#define glReadPixels probe_read
#define glTexImage2D probe_texture
#define glDrawArrays probe_draw
#define glFinish probe_finish
#define pp_gl_load state_load
#define glDrawBuffer state_draw_buffer
#define glBindTexture state_bind_texture
#define glDisable state_disable
#define glViewport state_viewport
#define glGetError state_error
#define SDL_GetWindowSizeInPixels state_size
#define SDL_GL_MakeCurrent probe_current
#define SDL_GL_SwapWindow probe_swap
#define pp_renderer_upload_source pp_raw_renderer_upload_source
#define pp_renderer_create pp_raw_renderer_create
#define pp_renderer_transform pp_raw_renderer_transform
#define pp_renderer_transform_source pp_raw_renderer_transform_source
#define pp_renderer_draw pp_raw_renderer_draw
#define pp_renderer_draw_source pp_raw_renderer_draw_source
#define pp_renderer_complete pp_raw_renderer_complete
#include "../../src/render/frame.c"
#undef pp_renderer_create
#undef pp_renderer_transform
#undef pp_renderer_transform_source
#undef pp_renderer_draw
#undef pp_renderer_draw_source
#undef pp_renderer_complete
#undef pp_renderer_upload_source
bool pp_renderer_upload_source(PPRenderer *renderer,const PPFrame *source,uint64_t identity,uint64_t revision,PPError *error) {
  double start=pp_probe_clock();bool ok=pp_raw_renderer_upload_source(renderer,source,identity,revision,error);pp_probe_record(PROBE_UPLOAD,start);return ok;
}
static bool complete_measurement(PPRenderer *renderer,PPError *error) {
  if(!pp_probe_completion_enabled) return true;
  double start=pp_probe_clock(); bool ok=pp_raw_renderer_complete(renderer,error); pp_probe_record(PROBE_GPU_COMPLETE,start);
  if(ok) query_measurement();else discard_query();
  return ok;
}
bool pp_renderer_complete(PPRenderer *renderer,PPError *error) {
  double start=pp_probe_clock(); bool ok=pp_raw_renderer_complete(renderer,error); pp_probe_record(PROBE_GPU_COMPLETE,start);
  if(ok) query_measurement();else discard_query(); return ok;
}
bool pp_renderer_draw(PPRenderer *renderer,PPVec2 layout_size,double longest,double angle,double opacity,double density,PPError *error) {
  double start=pp_probe_clock();
  measured_draw=true;
  bool ok=pp_raw_renderer_draw(renderer,layout_size,longest,angle,opacity,density,error);
  measured_draw=false;
  if(ok) ok=complete_measurement(renderer,error);
  else discard_query();
  pp_probe_record(PROBE_TRANSFORM,start);return ok;
}
bool pp_renderer_draw_source(PPRenderer *renderer,PPVec2 layout,PPRect rect,double longest,double angle,double opacity,double density,PPError *error) {
  if(!pp_probe_metrics[PROBE_TRANSFORM].count) {
    GLint flags=0;glGetIntegerv(GL_CONTEXT_FLAGS,&flags);
    printf("DIRECT error_policy=%s actual_context_flags=%d debug_context=%s\n",renderer->error_callback?"synchronous-KHR-debug-callback":"glGetError-fallback",flags,renderer->error_callback?"verified":"optional-unavailable");
  }
  double start=pp_probe_clock();measured_draw=true;
  bool ok=pp_raw_renderer_draw_source(renderer,layout,rect,longest,angle,opacity,density,error);measured_draw=false;
  if(ok) ok=complete_measurement(renderer,error);else discard_query();
  pp_probe_record(PROBE_TRANSFORM,start);return ok;
}
bool pp_renderer_create(PPRenderer **out, PPError *error) {
  double start = pp_probe_clock(); bool ok = pp_raw_renderer_create(out, error);
  pp_probe_record(PROBE_RENDERER_CREATE, start); return ok;
}
bool pp_renderer_transform(PPRenderer *renderer, const PPFrame *source, double longest,
  double angle, double opacity, double density, PPFrame *out, PPError *error) {
  double start = pp_probe_clock();
  measured_draw=true;
  bool ok = pp_raw_renderer_transform(renderer, source, longest, angle, opacity, density, out, error);
  measured_draw=false;
  if(ok) ok=complete_measurement(renderer,error);
  else discard_query();
  pp_probe_record(PROBE_TRANSFORM, start); return ok;
}
bool pp_renderer_transform_source(PPRenderer *renderer,const PPFrame *source,PPVec2 layout,PPRect rect,double longest,double angle,double opacity,double density,PPFrame *out,PPError *error) {
  double start=pp_probe_clock();measured_draw=true;
  bool ok=pp_raw_renderer_transform_source(renderer,source,layout,rect,longest,angle,opacity,density,out,error);measured_draw=false;
  if(ok)ok=complete_measurement(renderer,error);else discard_query();pp_probe_record(PROBE_TRANSFORM,start);return ok;
}
