/* Exercise the real Windows observer with controlled OS boundaries. No hook
 * is installed, window created, input injected or foreground changed. */
#include "../../src/platform/windows/windows_internal.h"
#include "../test.h"
static unsigned forwarded, queued;
static unsigned installed, uninstalled;
static bool reject_enqueue;
static PPPlatformEvent recorded;
static HWND lower_window = (HWND)(uintptr_t)100;
static HWND top_window = (HWND)(uintptr_t)100;
static PPSurface *dead_surface;
static HWND WINAPI observed_window(POINT point) {
  (void)point;
  return top_window;
}
static BOOL WINAPI observed_visible(HWND window) {
  return window==(HWND)(uintptr_t)1 || window==(HWND)(uintptr_t)2;
}
static HWND WINAPI observed_ancestor(HWND window,UINT flags) {
  CHECK(flags==GA_ROOT);return window;
}
static HHOOK WINAPI install_hook(int id, HOOKPROC callback, HINSTANCE module, DWORD thread) {
  CHECK(id == WH_MOUSE_LL && callback && module && !thread);
  installed++;
  return (HHOOK)(uintptr_t)1;
}
static BOOL WINAPI uninstall_hook(HHOOK value) {
  CHECK(value == (HHOOK)(uintptr_t)1);
  uninstalled++;
  return TRUE;
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
void pp_surface_hide(PPSurface *surface) {pp_surface_cancel_input(surface);}
bool pp_windows_enqueue(PPPlatformEvent event) {
  recorded = event;
  queued++;
  return !reject_enqueue;
}
PPPlatformEvent pp_windows_event(PPSurface *surface, UINT message, POINT point,
                                 DWORD data) {
  CHECK(surface && surface != dead_surface);
  (void)data;
  PPPlatformEventKind kind = message == WM_MOUSEWHEEL ? PP_EVENT_WHEEL :
    message == WM_RBUTTONDOWN ? PP_EVENT_BUTTON_DOWN :
    message == WM_RBUTTONUP ? PP_EVENT_BUTTON_UP : PP_EVENT_CANCEL;
  return (PPPlatformEvent){.kind = kind,
                           .button = (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP) ? PP_BUTTON_RIGHT : PP_BUTTON_NONE,
                           .surface = surface,
                           .instance_id = surface->instance_id,
                           .desktop_point = {point.x, point.y}};
}
#define WindowFromPoint observed_window
#define IsWindowVisible observed_visible
#define GetAncestor observed_ancestor
#define CallNextHookEx forward_input
#define SetWindowsHookExW install_hook
#define UnhookWindowsHookEx uninstall_hook
#include "../../src/platform/windows/input.c"
#undef WindowFromPoint
#undef IsWindowVisible
#undef GetAncestor
#undef CallNextHookEx
#undef SetWindowsHookExW
#undef UnhookWindowsHookEx

static LRESULT right(UINT message, LONG x, LONG y) {
  MSLLHOOKSTRUCT event = {.pt = {x,y}};
  return observe(HC_ACTION, message, (LPARAM)&event);
}
static void accepted_down(PPSurface *a) {
  top_window = a->window;
  unsigned before = queued, passed = forwarded;
  CHECK(right(WM_RBUTTONDOWN,0,0) == 1);
  CHECK(queued == before+1 && forwarded == passed);
  CHECK(recorded.surface == a && recorded.instance_id == 1);
  CHECK(recorded.kind == PP_EVENT_BUTTON_DOWN && recorded.button == PP_BUTTON_RIGHT);
}
static void outside_gesture(void) {
  top_window = lower_window;
  unsigned before = queued, passed = forwarded;
  CHECK(right(WM_RBUTTONDOWN,500,500) == 17);
  CHECK(right(WM_RBUTTONUP,500,500) == 17);
  CHECK(queued == before && forwarded == passed+2);
}
/* Removing consumed-right bookkeeping breaks cancellation; unhooking the
 * final surface before physical up breaks drain/re-registration. The real
 * observer is invoked with only native hook/routing boundaries controlled. */
static void right_lifetime(bool remove_last, bool direct_cancel) {
  installed = uninstalled = 0;
  uint8_t opaque[4] = {255,255,255,255};
  PPSurface a = {.window=(HWND)(uintptr_t)1,.instance_id=1,.width=1,.height=1,.bits=opaque};
  PPSurface b = {.window=(HWND)(uintptr_t)2,.instance_id=2};
  CHECK(pp_windows_input_add(&a,NULL));
  accepted_down(&a);
  unsigned before = queued, passed = forwarded;
  if(remove_last) {
    pp_windows_input_remove(&a);
    printf("final removal before release: unhook=%u active=%d\n",uninstalled,hook != NULL);
    CHECK(uninstalled == 0 && hook);
    /* The draining hook forwards other buttons and wheel with no surfaces. */
    MSLLHOOKSTRUCT outside={.pt={500,500}};
    CHECK(observe(HC_ACTION,WM_LBUTTONDOWN,(LPARAM)&outside) == 17);
    CHECK(observe(HC_ACTION,WM_LBUTTONUP,(LPARAM)&outside) == 17);
    CHECK(observe(HC_ACTION,WM_MOUSEWHEEL,(LPARAM)&outside) == 17);
    CHECK(queued == before && forwarded == passed+3);
    passed=forwarded;
  } else if(direct_cancel) pp_surface_cancel_input(&a);
  else pp_windows_input_cancel(&a);
  dead_surface = &a;
  memset(&a,0xa5,sizeof(a)); /* No conversion may touch this canceled storage. */
  pp_surface_cancel_input(&b);
  pp_platform_release_capture();
  top_window = lower_window;
  LRESULT result = right(WM_RBUTTONUP,700,800);
  printf("right lifetime remove_last=%d direct_cancel=%d: release=%lld forwarded=%u queued=%u unhook=%u\n",
         remove_last,direct_cancel,(long long)result,forwarded-passed,queued-before,uninstalled);
  CHECK(result == 1 && forwarded == passed && queued == before);
  if(remove_last) CHECK(uninstalled == 1 && !hook);
  dead_surface = NULL;
  a = (PPSurface){.window=(HWND)(uintptr_t)1,.instance_id=1,.width=1,.height=1,.bits=opaque};
  outside_gesture();
  if(!remove_last) pp_windows_input_remove(&a);
  CHECK(!hook && uninstalled == 1);
  puts("PASS accepted right cancellation/deletion consumes release without stale conversion; outside input recovers");
}
static void right_boundaries(void) {
  installed = uninstalled = 0;
  uint8_t opaque[4] = {0,0,0,255};
  PPSurface a = {.window=(HWND)(uintptr_t)1,.instance_id=1,.width=1,.height=1,.bits=opaque};
  PPSurface b = {.window=(HWND)(uintptr_t)2,.instance_id=2};
  CHECK(pp_windows_input_add(&a,NULL));
  accepted_down(&a);
  pp_surface_cancel_input(&b);
  pp_platform_release_capture();
  CHECK(right(WM_RBUTTONUP,700,800) == 1);
  CHECK(recorded.kind == PP_EVENT_BUTTON_UP && recorded.surface == &a);
  outside_gesture();
  top_window=a.window;reject_enqueue=true;
  CHECK(right(WM_RBUTTONDOWN,0,0) == 17);
  unsigned before=queued;
  CHECK(right(WM_RBUTTONUP,0,0) == 17 && queued == before);
  reject_enqueue=false;accepted_down(&a);reject_enqueue=true;
  CHECK(right(WM_RBUTTONUP,700,800) == 1);
  reject_enqueue=false;outside_gesture();
  top_window=a.window;opaque[3]=0;
  CHECK(right(WM_RBUTTONDOWN,0,0) == 17 && right(WM_RBUTTONUP,0,0) == 17);
  opaque[3]=1;accepted_down(&a);
  pp_windows_input_remove(&a);
  CHECK(hook && uninstalled == 0);
  /* Reusing the exact address must not inherit the canceled event target. */
  a.instance_id=99;CHECK(pp_windows_input_add(&a,NULL));
  CHECK(installed == 1);
  before=queued;
  CHECK(right(WM_RBUTTONUP,0,0) == 1 && queued == before);
  CHECK(hook && uninstalled == 0);
  outside_gesture();pp_windows_input_remove(&a);
  CHECK(!hook && uninstalled == 1);
  puts("PASS rejected down/up, alpha/native-top authority, batched release, moved release and registration during right drain");
}

static LRESULT outside_wheel(void) {
  MSLLHOOKSTRUCT event = {.pt = {500, 500},
                          .mouseData = (DWORD)WHEEL_DELTA << 16};
  return observe(HC_ACTION, WM_MOUSEWHEEL, (LPARAM)&event);
}
int main(int argc, char **argv) {
  if(argc == 2) {
    if(!strcmp(argv[1],"--right-cancel")) right_lifetime(false,false);
    else if(!strcmp(argv[1],"--right-detach")) right_lifetime(false,true);
    else if(!strcmp(argv[1],"--right-remove")) right_lifetime(true,false);
    else {CHECK(!strcmp(argv[1],"--right-boundaries"));right_boundaries();}
    return 0;
  }
  right_lifetime(false,false);
  right_lifetime(false,true);
  right_lifetime(true,false);
  right_boundaries();
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
