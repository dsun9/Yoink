#include "performance_probe.h"
#include <SDL3/SDL.h>
static bool probe_size(SDL_Window *window,int width,int height) {
  double start=pp_probe_clock();bool ok=SDL_SetWindowSize(window,width,height);pp_probe_record(PROBE_NATIVE_SIZE,start);return ok;
}
static BOOL WINAPI probe_place(HWND window,HWND after,int x,int y,int width,int height,UINT flags) {
  double start=pp_probe_clock();BOOL ok=SetWindowPos(window,after,x,y,width,height,flags);pp_probe_record(PROBE_NATIVE_PLACE,start);return ok;
}
#define SDL_SetWindowSize probe_size
#define SetWindowPos probe_place
#define pp_surface_place_gl pp_raw_surface_place_gl
#include "../../src/platform/windows/direct_gl.c"
#undef pp_surface_place_gl
bool pp_surface_place_gl(PPSurface *surface,PPRect bounds,bool interactive,PPError *error) {
  double start=pp_probe_clock();
  bool ok=pp_raw_surface_place_gl(surface,bounds,interactive,error);
  pp_probe_record(PROBE_SURFACE,start);return ok;
}
