#include "performance_probe.h"
#define pp_surface_place_gl pp_raw_surface_place_gl
#include "../../src/platform/windows/direct_gl.c"
#undef pp_surface_place_gl
bool pp_surface_place_gl(PPSurface *surface,PPRect bounds,bool interactive,PPError *error) {
  double start=pp_probe_clock();
  bool ok=pp_raw_surface_place_gl(surface,bounds,interactive,error);
  pp_probe_record(PROBE_SURFACE,start);return ok;
}
