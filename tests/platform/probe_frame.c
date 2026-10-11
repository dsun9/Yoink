#include "performance_probe.h"
#include "../../src/render/gl_api.h"
static GLuint elapsed_query;
static PFNGLGENQUERIESPROC query_gen;
static PFNGLBEGINQUERYPROC query_begin;
static PFNGLENDQUERYPROC query_end;
static PFNGLGETQUERYOBJECTUI64VPROC query_result;
static PFNGLDELETEQUERIESPROC query_delete;
static void APIENTRY probe_draw(GLenum mode, GLint first, GLsizei count) {
  if(pp_probe_completion_enabled && SDL_GL_ExtensionSupported("GL_ARB_timer_query")) {
    query_gen=(PFNGLGENQUERIESPROC)SDL_GL_GetProcAddress("glGenQueries");
    query_begin=(PFNGLBEGINQUERYPROC)SDL_GL_GetProcAddress("glBeginQuery");
    query_end=(PFNGLENDQUERYPROC)SDL_GL_GetProcAddress("glEndQuery");
    query_result=(PFNGLGETQUERYOBJECTUI64VPROC)SDL_GL_GetProcAddress("glGetQueryObjectui64v");
    query_delete=(PFNGLDELETEQUERIESPROC)SDL_GL_GetProcAddress("glDeleteQueries");
    if(query_gen && query_begin && query_end && query_result && query_delete) {
      query_gen(1,&elapsed_query); query_begin(GL_TIME_ELAPSED,elapsed_query);
    }
  }
  double start=pp_probe_clock(); glDrawArrays(mode,first,count); pp_probe_record(PROBE_GPU_SUBMIT,start);
  if(elapsed_query) query_end(GL_TIME_ELAPSED);
}
static void query_measurement(void) {
  if(elapsed_query) {
    GLuint64 ns=0; query_result(elapsed_query,GL_QUERY_RESULT,&ns);
    double ms=ns/1000000.0; PPProbeMetric *m=&pp_probe_metrics[PROBE_GPU_EXECUTION];
    m->count++; m->ms+=ms; if(ms>m->max_ms) m->max_ms=ms;
    query_delete(1,&elapsed_query); elapsed_query=0;
  }
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
#define pp_renderer_create pp_raw_renderer_create
#define pp_renderer_transform pp_raw_renderer_transform
#define pp_renderer_draw pp_raw_renderer_draw
#define pp_renderer_complete pp_raw_renderer_complete
#include "../../src/render/frame.c"
#undef pp_renderer_create
#undef pp_renderer_transform
#undef pp_renderer_draw
#undef pp_renderer_complete
static bool complete_measurement(PPRenderer *renderer,PPError *error) {
  if(!pp_probe_completion_enabled) return true;
  double start=pp_probe_clock(); bool ok=pp_raw_renderer_complete(renderer,error); pp_probe_record(PROBE_GPU_COMPLETE,start);
  if(ok) query_measurement();
  return ok;
}
bool pp_renderer_complete(PPRenderer *renderer,PPError *error) {
  double start=pp_probe_clock(); bool ok=pp_raw_renderer_complete(renderer,error); pp_probe_record(PROBE_GPU_COMPLETE,start);
  if(ok) query_measurement(); return ok;
}
bool pp_renderer_draw(PPRenderer *renderer,PPVec2 layout_size,double longest,double angle,double opacity,double density,PPError *error) {
  double start=pp_probe_clock();
  bool ok=pp_raw_renderer_draw(renderer,layout_size,longest,angle,opacity,density,error);
  if(ok) ok=complete_measurement(renderer,error);
  pp_probe_record(PROBE_TRANSFORM,start);return ok;
}
bool pp_renderer_create(PPRenderer **out, PPError *error) {
  double start = pp_probe_clock(); bool ok = pp_raw_renderer_create(out, error);
  pp_probe_record(PROBE_RENDERER_CREATE, start); return ok;
}
bool pp_renderer_transform(PPRenderer *renderer, const PPFrame *source, double longest,
  double angle, double opacity, double density, PPFrame *out, PPError *error) {
  double start = pp_probe_clock();
  bool ok = pp_raw_renderer_transform(renderer, source, longest, angle, opacity, density, out, error);
  if(ok) ok=complete_measurement(renderer,error);
  pp_probe_record(PROBE_TRANSFORM, start); return ok;
}
