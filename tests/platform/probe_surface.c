#include "performance_probe.h"
static HBITMAP WINAPI probe_dib(HDC dc,const BITMAPINFO *info,UINT usage,void **bits,HANDLE section,DWORD offset) {
  double start=pp_probe_clock(); HBITMAP result=CreateDIBSection(dc,info,usage,bits,section,offset); pp_probe_record(PROBE_DIB,start); return result;
}
static BOOL WINAPI probe_layered(HWND window,HDC dst,POINT *point,SIZE *size,HDC src,POINT *origin,COLORREF key,BLENDFUNCTION *blend,DWORD flags) {
  double start=pp_probe_clock(); BOOL result=UpdateLayeredWindow(window,dst,point,size,src,origin,key,blend,flags); pp_probe_record(PROBE_LAYERED,start); return result;
}
#define CreateDIBSection probe_dib
#define UpdateLayeredWindow probe_layered
#define pp_surface_present pp_raw_surface_present
#include "../../src/platform/windows/surface.c"
#undef pp_surface_present
bool pp_surface_present(PPSurface *surface,const PPFrame *frame,PPRect bounds,PPError *error) {
  double start=pp_probe_clock(); bool ok=pp_raw_surface_present(surface,frame,bounds,error); pp_probe_record(PROBE_SURFACE,start); return ok;
}
