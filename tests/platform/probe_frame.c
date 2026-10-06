#include "performance_probe.h"
#include "../../src/render/gl_api.h"
static void APIENTRY probe_read(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels) {
  double start=pp_probe_clock(); glReadPixels(x,y,w,h,format,type,pixels); pp_probe_record(PROBE_READBACK,start);
}
static void APIENTRY probe_texture(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *pixels) {
  double start=pp_probe_clock(); glTexImage2D(target,level,internal,w,h,border,format,type,pixels); pp_probe_record(PROBE_TEXTURE,start);
}
#define glReadPixels probe_read
#define glTexImage2D probe_texture
#define pp_renderer_create pp_raw_renderer_create
#define pp_renderer_transform pp_raw_renderer_transform
#include "../../src/render/frame.c"
#undef pp_renderer_create
#undef pp_renderer_transform
bool pp_renderer_create(PPRenderer **out, PPError *error) {
  double start = pp_probe_clock(); bool ok = pp_raw_renderer_create(out, error);
  pp_probe_record(PROBE_RENDERER_CREATE, start); return ok;
}
bool pp_renderer_transform(PPRenderer *renderer, const PPFrame *source, double longest,
  double angle, double opacity, double density, PPFrame *out, PPError *error) {
  double start = pp_probe_clock();
  bool ok = pp_raw_renderer_transform(renderer, source, longest, angle, opacity, density, out, error);
  pp_probe_record(PROBE_TRANSFORM, start); return ok;
}
