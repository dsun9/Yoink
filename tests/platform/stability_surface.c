#include <windows.h>
void pp_test_sprite_placed(void);
static BOOL observed_set_window_pos(HWND window, HWND after, int x, int y, int w, int h, UINT flags) {
  BOOL ok = SetWindowPos(window, after, x, y, w, h, flags);
  if (ok) pp_test_sprite_placed();
  return ok;
}
/* Hold the application at the completed native placement boundary so Desktop
 * Duplication can measure the previous frame before the next GL publication. */
#define SetWindowPos observed_set_window_pos
#include "../../src/platform/windows/direct_gl.c"
