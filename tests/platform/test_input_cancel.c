/* Exercise the real Windows observer with controlled OS boundaries. No hook
 * is installed, window created, input injected or foreground changed. */
#include "../../src/platform/windows/windows_internal.h"
#include "../test.h"
static unsigned forwarded, queued;
static PPPlatformEvent recorded;
static HWND lower_window = (HWND)(uintptr_t)100;
static HWND WINAPI observed_window(POINT point) {
  (void)point;
  return lower_window;
}
static LRESULT WINAPI forward_input(HHOOK value, int code, WPARAM w, LPARAM l) {
  (void)value;
  (void)code;
  (void)w;
  (void)l;
  forwarded++;
  return 17;
}
HWND pp_windows_drawer_window(void) { return NULL; }
bool pp_windows_enqueue(PPPlatformEvent event) {
  recorded = event;
  queued++;
  return true;
}
PPPlatformEvent pp_windows_event(PPSurface *surface, UINT message, POINT point,
                                 DWORD data) {
  (void)message;
  (void)data;
  return (PPPlatformEvent){.kind = PP_EVENT_WHEEL,
                           .surface = surface,
                           .instance_id = surface->instance_id,
                           .desktop_point = {point.x, point.y}};
}
#define WindowFromPoint observed_window
#define CallNextHookEx forward_input
#include "../../src/platform/windows/input.c"
#undef WindowFromPoint
#undef CallNextHookEx

static LRESULT outside_wheel(void) {
  MSLLHOOKSTRUCT event = {.pt = {500, 500},
                          .mouseData = (DWORD)WHEEL_DELTA << 16};
  return observe(HC_ACTION, WM_MOUSEWHEEL, (LPARAM)&event);
}
int main(void) {
  uint8_t opaque[4] = {255, 255, 255, 255};
  PPSurface a = {.window = (HWND)(uintptr_t)1,
                 .instance_id = 1,
                 .width = 1,
                 .height = 1,
                 .bits = opaque};
  PPSurface b = {.window = (HWND)(uintptr_t)2, .instance_id = 2};
  surfaces = &a;
  hook = (HHOOK)(uintptr_t)1; /* Only eligibility; never passed to an OS API. */
  for (int captured = 0; captured < 2; captured++) {
    accepted_press = &a;
    capture_eligible = true;
    button_targets[PP_BUTTON_LEFT] = &a;
    if (captured)
      CHECK(pp_platform_capture(&a, NULL));
    CHECK(pp_windows_input_owned(&a));
    if (captured) {
      CHECK(outside_wheel() == 1);
      CHECK(recorded.surface == &a);
    }
    unsigned before = queued;
    pp_surface_cancel_input(&a);
    CHECK(!pp_windows_input_owner() && !capture_eligible);
    CHECK(!button_targets[PP_BUTTON_LEFT] && queued == before);
    CHECK(!pp_platform_capture(&a, NULL));
    unsigned passed = forwarded;
    CHECK(outside_wheel() == 17 && forwarded == passed + 1 && queued == before);
    /* Synchronization of A must not change B's pending/owned route. */
    accepted_press = &b;
    capture_eligible = true;
    button_targets[PP_BUTTON_LEFT] = &b;
    if (captured)
      CHECK(pp_platform_capture(&b, NULL));
    pp_surface_cancel_input(&a);
    CHECK(pp_windows_input_owner() == &b && capture_eligible);
    CHECK(button_targets[PP_BUTTON_LEFT] == &b && queued == before);
    if (captured) {
      CHECK(outside_wheel() == 1 && recorded.surface == &b);
    }
    pp_surface_cancel_input(&b);
  }
  puts(
      "PASS Windows pending/owned cancellation restores original outside-wheel "
      "forwarding and preserves unrelated ownership; no desktop APIs invoked");
  return 0;
}
