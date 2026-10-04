/* Real hidden native surfaces and DIBs. Only upload/placement are controlled;
 * no input is synthesized and no window is ever shown or activated. */
#include "../../src/platform/windows/windows_internal.h"
#include "../test.h"
static bool reject_upload;
static BOOL WINAPI controlled_upload(HWND window, HDC screen,
                                     POINT *destination, SIZE *size, HDC dc,
                                     POINT *source, COLORREF key,
                                     BLENDFUNCTION *blend, DWORD flags) {
  (void)window;
  (void)screen;
  (void)destination;
  (void)size;
  (void)dc;
  (void)source;
  (void)key;
  (void)blend;
  (void)flags;
  if (reject_upload) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  return TRUE;
}
static BOOL WINAPI hidden_placement(HWND window, HWND after, int x, int y,
                                    int w, int h, UINT flags) {
  (void)window;
  (void)after;
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)flags;
  return TRUE;
}
#define UpdateLayeredWindow controlled_upload
#define SetWindowPos hidden_placement
#include "../../src/platform/windows/surface.c"
#undef UpdateLayeredWindow
#undef SetWindowPos
int main(void) {
  HWND foreground = GetForegroundWindow();
  PPSurface *surface = NULL;
  PPError error = {0};
  CHECK(pp_surface_create(&surface, &error));
  uint8_t pixels[64 * 64 * 4] = {0}, alpha[64 * 64] = {0};
  PPFrame first = {pixels, alpha, 16, 16, 64};
  CHECK(pp_surface_present(surface, &first, (PPRect){0, 0, 16, 16}, &error));
  CHECK(surface->bitmap && surface->bits && !IsWindowVisible(surface->window));
  /* Size change replaces the old native bitmap before upload can fail. */
  PPFrame second = {pixels, alpha, 64, 64, 256};
  reject_upload = true;
  CHECK(!pp_surface_present(surface, &second, (PPRect){0, 0, 64, 64}, &error));
  CHECK(error.code == PP_ERROR_PLATFORM);
  CHECK(!surface->bitmap && !surface->bits && !surface->window);
  CHECK(!pp_windows_input_owned(surface));
  pp_surface_destroy(surface);
  CHECK(GetForegroundWindow() == foreground);
  puts("PASS failed resized native upload destroys interceptor and both "
       "old/candidate DIBs before return; no input or focus changes");
  return 0;
}
