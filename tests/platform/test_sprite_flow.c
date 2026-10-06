#include "../../src/app/presenter_internal.h"
#include "../../src/app/runtime_internal.h"
#include "../../src/platform/windows/windows_internal.h"
#include "../../src/ui/backend.h"
#include "../../src/ui/drawer_internal.h"
#include "../../src/ui/settings_internal.h"
#include "../test.h"
#include "pointy/runtime.h"
#include "receiver_shared.h"
#include <math.h>
#include <string.h>
#include <windows.h>

static void check_hidden(PPDrawer *drawer) {
  CHECK(!drawer->backend->visible);
  HWND window =
      SDL_GetPointerProperty(SDL_GetWindowProperties(drawer->backend->window),
                             SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
  CHECK(window && !IsWindowVisible(window) && GetForegroundWindow() != window);
}
static void hidden_drawer(void) {
  PPApp *app = NULL;
  PPDrawer *drawer = NULL;
  PPError error = {0};
  uint64_t id = 0;
  CHECK(pp_app_create(&app, &(PPAppOptions){.preferences_directory = ""},
                      &error));
  CHECK(pp_scene_spawn(pp_app_scene(app), pp_app_project(app),
                       PP_ANCHOR_BOTTOM_RIGHT, (PPVec2){790, 590}, 1, &id,
                       &error));
  CHECK(pp_drawer_open(&drawer, app, id, &error));
  drawer->render_hidden = true;
  PPDisplay display = {1, {0, 0, 800, 600}, 1, 1};
  pp_drawer_layout(drawer, &display, 1);
  CHECK(drawer->bounds.x >= 0 && drawer->bounds.y >= 0 &&
        drawer->bounds.x + drawer->bounds.w <= 800 &&
        drawer->bounds.y + drawer->bounds.h <= 600);
  CHECK(pp_drawer_draw(drawer, 1, &error));
  check_hidden(drawer);
  drawer->backend->capture_path = PP_FLOW_OUTPUT "/drawer-hand.bmp";
  CHECK(pp_drawer_draw(drawer, 1.1, &error));
  SDL_Surface *proof = SDL_LoadBMP(PP_FLOW_OUTPUT "/drawer-hand.bmp");
  CHECK(proof);
  SDL_Surface *rgba = SDL_ConvertSurface(proof, SDL_PIXELFORMAT_RGBA32);
  CHECK(rgba);
  size_t yellow = 0;
  for (int y = 0; y < rgba->h; y++)
    for (int x = 0; x < rgba->w; x++) {
      const uint8_t *p =
          (uint8_t *)rgba->pixels + (size_t)y * rgba->pitch + x * 4;
      if (p[0] > 170 && p[1] > 95 && p[2] < 90)
        yellow++;
    }
  CHECK(yellow >
        25); /* The default emoji must be identifiable in the selector. */
  SDL_DestroySurface(rgba);
  SDL_DestroySurface(proof);
  const PPAnchorList *list =
      pp_project_anchor(pp_app_project(app), PP_ANCHOR_BOTTOM_RIGHT);
  uint64_t item = list->default_id,
           revision = pp_project_revision(pp_app_project(app));
  char control[64];
  snprintf(control, sizeof(control), "drawer-item-%llu",
           (unsigned long long)item);
  PPRect button;
  CHECK(pp_ui_control_rect(drawer->backend, control, &button));
  CHECK(button.y >= 0 && button.y + button.h < drawer->backend->height);
  PPRect close;
  CHECK(pp_ui_control_rect(drawer->backend, "drawer-close", &close));
  CHECK(close.y >= 0 && close.y + close.h <= drawer->backend->height);
  pp_ui_focus(drawer->backend, control);
  /* A density-only transition leaves native control bounds unchanged but
   * invalidates atlas/thumbnail resources and preserves the focused item. */
  uint64_t saved_focus = drawer->backend->focus_id;
  display.drawable_pixels_per_dip = 2;
  pp_drawer_layout(drawer, &display, 1);
  CHECK(!drawer->backend && drawer->focus_id == saved_focus);
  CHECK(!drawer->dismissed && drawer->bounds.w == 340);
  display.drawable_pixels_per_dip = 1;
  pp_drawer_layout(drawer, &display, 1);
  CHECK(pp_drawer_draw(drawer, 1.2, &error));
  CHECK(drawer->backend->focus_id == saved_focus);
  pp_drawer_event(
      drawer,
      &(PPPlatformEvent){.kind = PP_EVENT_KEY_DOWN,
                         .window_id = pp_ui_backend_window_id(drawer->backend),
                         .key = PP_KEY_ENTER});
  CHECK(pp_drawer_draw(drawer, 2, &error));
  CHECK(drawer->dismissed);
  CHECK(pp_project_revision(pp_app_project(app)) == revision &&
        !pp_app_dirty(app));
  CHECK(pp_scene_get(pp_app_scene(app), id)->animation_start == 2);
  pp_drawer_close(drawer);
  drawer = NULL;
  CHECK(pp_drawer_open(&drawer, app, id, &error));
  drawer->render_hidden = true;
  pp_drawer_layout(drawer, &display, 1);
  CHECK(pp_app_command(app,
                       &(PPCommand){.kind = PP_COMMAND_REMOVE_ITEM,
                                    .anchor = PP_ANCHOR_BOTTOM_RIGHT,
                                    .item_id = item},
                       &error));
  CHECK(pp_drawer_draw(drawer, 3, &error));
  CHECK(pp_drawer_count(drawer) == 0);
  drawer->backend->capture_path = PP_FLOW_OUTPUT "/drawer-empty.bmp";
  CHECK(pp_drawer_draw(drawer, 3, &error));
  pp_drawer_event(
      drawer,
      &(PPPlatformEvent){.kind = PP_EVENT_KEY_DOWN,
                         .window_id = pp_ui_backend_window_id(drawer->backend),
                         .key = PP_KEY_ESCAPE});
  CHECK(drawer->dismissed && drawer->restore_focus);
  check_hidden(drawer);
  pp_drawer_close(drawer);
  CHECK(pp_drawer_open(&drawer, app, id, &error));
  drawer->render_hidden = true;
  pp_drawer_layout(drawer, &display, 1);
  CHECK(pp_drawer_draw(drawer, 4, &error));
  pp_drawer_event(drawer, &(PPPlatformEvent){.kind = PP_EVENT_BUTTON_DOWN,
                                             .desktop_point = {10, 10},
                                             .button = PP_BUTTON_LEFT});
  CHECK(drawer->dismissed && drawer->restore_focus);
  pp_drawer_close(drawer);
  CHECK(pp_drawer_open(&drawer, app, id, &error));
  drawer->render_hidden = true;
  pp_drawer_layout(drawer, &display, 1);
  CHECK(pp_drawer_draw(drawer, 5, &error));
  pp_drawer_event(drawer,
                  &(PPPlatformEvent){
                      .kind = PP_EVENT_FOCUS_LOST,
                      .window_id = pp_ui_backend_window_id(drawer->backend)});
  CHECK(drawer->dismissed && !drawer->restore_focus);
  pp_drawer_close(drawer);
  pp_app_destroy(app);
  puts("PASS hidden drawer color emoji, bounds, keyboard replacement, clean "
       "revision, deleted item, empty state and Escape");
}
static void hidden_long_list(void) {
  PPApp *app = NULL;
  PPError error = {0};
  uint64_t instance = 0, last = 0;
  CHECK(pp_app_create(&app, &(PPAppOptions){.preferences_directory = ""},
                      &error));
  PPItem hand = *pp_project_target(pp_app_project(app), PP_ANCHOR_TOP_LEFT);
  hand.id = 0;
  for (int i = 0; i < 24; i++) {
    CHECK(pp_app_command(app,
                         &(PPCommand){.kind = PP_COMMAND_ADD_ITEM,
                                      .anchor = PP_ANCHOR_TOP_LEFT,
                                      .item = &hand},
                         &error));
    last = pp_app_ui(app)->selected_item;
  }
  CHECK(pp_scene_spawn(pp_app_scene(app), pp_app_project(app),
                       PP_ANCHOR_TOP_LEFT, (PPVec2){-2, 598}, 1, &instance,
                       &error));
  const float scales[] = {1, 1.5f, 2};
  for (size_t i = 0; i < 3; i++) {
    PPDrawer *drawer = NULL;
    CHECK(pp_drawer_open(&drawer, app, instance, &error));
    drawer->render_hidden = true;
    PPDisplay display = {
        1,
        {-800 * scales[i], 0, 800 * scales[i], 600 * scales[i]},
        scales[i],
        scales[i]};
    pp_drawer_layout(drawer, &display, 1);
    CHECK(drawer->bounds.x >= display.bounds.x &&
          drawer->bounds.x + drawer->bounds.w <= 0);
    CHECK(pp_drawer_draw(drawer, 2, &error));
    char control[64], path[256];
    snprintf(control, sizeof(control), "drawer-item-%llu",
             (unsigned long long)last);
    pp_ui_focus(drawer->backend, control);
    CHECK(pp_drawer_draw(drawer, 2, &error));
    CHECK(pp_drawer_draw(drawer, 2, &error));
    PPRect row, close;
    CHECK(pp_ui_control_rect(drawer->backend, control, &row));
    CHECK(pp_ui_control_rect(drawer->backend, "drawer-close", &close));
    CHECK(row.y > 60 && row.y + row.h < close.y &&
          close.y + close.h <= drawer->backend->height);
    snprintf(path, sizeof(path), PP_FLOW_OUTPUT "/drawer-long-%zu.bmp", i);
    drawer->backend->capture_path = path;
    CHECK(pp_drawer_draw(drawer, 2, &error));
    check_hidden(drawer);
    display.desktop_units_per_dip = display.drawable_pixels_per_dip = 1;
    display.bounds = (PPRect){-800, 0, 800, 600};
    pp_drawer_layout(drawer, &display, 1);
    CHECK(pp_drawer_draw(drawer, 2, &error));
    CHECK(drawer->backend->scale == 1 && drawer->backend->width == 340);
    pp_drawer_close(drawer);
  }
  pp_app_destroy(app);
  puts("PASS long drawer keyboard scroll and clamping at 100%, 150%, 200% with "
       "negative display origin");
}
static PPApp *flow_app;
static PPRuntime *flow;
static PPSettings *flow_settings;
static ReceiverState *receiver;
static PROCESS_INFORMATION child;
static HANDLE mapping, watchdog_stop, watchdog;
static POINT old_cursor;
static HWND old_foreground;
static bool left_held, right_held, alt_held, control_held, desktop_started;
static PPDisplay simulated[2];
static size_t simulated_count;
static double frozen_time;
static double now(void) {
  return frozen_time > 0 ? frozen_time
                         : (double)SDL_GetPerformanceCounter() /
                               SDL_GetPerformanceFrequency();
}
static bool restore_desktop(void) {
  INPUT up[8] = {0};
  UINT n = 0;
  if (left_held || right_held) {
    up[n].type = INPUT_MOUSE;
    up[n++].mi.dwFlags = (left_held ? MOUSEEVENTF_LEFTUP : 0) |
                         (right_held ? MOUSEEVENTF_RIGHTUP : 0);
  }
  if (alt_held) {
    up[n].type = INPUT_KEYBOARD;
    up[n].ki.wVk = VK_MENU;
    up[n++].ki.dwFlags = KEYEVENTF_KEYUP;
  }
  if (control_held) {
    up[n].type = INPUT_KEYBOARD;
    up[n].ki.wVk = VK_CONTROL;
    up[n++].ki.dwFlags = KEYEVENTF_KEYUP;
  }
  /* A partial SendInput batch can stop after any key-down. Release every
   * ordinary key used by this harness as well as its tracked modifiers. */
  const WORD keys[] = {VK_ESCAPE, 'A', 'N'};
  for (size_t i = 0; i < 3; i++) {
    up[n].type = INPUT_KEYBOARD;
    up[n].ki.wVk = keys[i];
    up[n++].ki.dwFlags = KEYEVENTF_KEYUP;
  }
  bool sent = !n || SendInput(n, up, sizeof(INPUT)) == n;
  left_held = right_held = alt_held = control_held = false;
  SetCursorPos(old_cursor.x, old_cursor.y);
  if (old_foreground && IsWindow(old_foreground)) {
    /* Cross-thread activation is asynchronous. WM_NULL completion is not a
     * foreground transition acknowledgement, especially after receiver exit.
     * Wait on the actual condition while servicing our own native messages. */
    ULONGLONG deadline=GetTickCount64()+1000;
    do {
      SetForegroundWindow(old_foreground);
      MSG message;
      while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)) {
        TranslateMessage(&message);DispatchMessageW(&message);
      }
      if(GetForegroundWindow()==old_foreground) break;
      Sleep(10);
    } while(IsWindow(old_foreground) && GetTickCount64()<deadline);
  }
  POINT actual = {0};
  GetCursorPos(&actual);
  bool cursor = actual.x == old_cursor.x && actual.y == old_cursor.y;
  bool valid = old_foreground && IsWindow(old_foreground);
  bool focus = valid && GetForegroundWindow() == old_foreground;
  bool released = !(GetAsyncKeyState(VK_LBUTTON) & 0x8000) &&
                  !(GetAsyncKeyState(VK_RBUTTON) & 0x8000) &&
                  !(GetAsyncKeyState(VK_MENU) & 0x8000) &&
                  !(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                  !(GetAsyncKeyState(VK_ESCAPE) & 0x8000) &&
                  !(GetAsyncKeyState('A') & 0x8000) &&
                  !(GetAsyncKeyState('N') & 0x8000);
  printf("RESTORE input=%s cursor=%s original-window-valid=%s foreground=%s\n",
         sent && released ? "released" : "FAILED",
         cursor ? "restored" : "FAILED", valid ? "yes" : "no",
         !valid  ? "unavailable"
         : focus ? "restored"
                 : "FAILED");
  fflush(stdout);
  return sent && released && cursor && focus;
}
static void cleanup(void) {
  if (!desktop_started)
    return;
  if (watchdog_stop)
    SetEvent(watchdog_stop);
  pp_runtime_destroy(flow);
  flow = NULL;
  pp_settings_destroy(flow_settings);
  flow_settings = NULL;
  pp_platform_release_capture();
  pp_app_destroy(flow_app);
  flow_app = NULL;
  if (receiver && receiver->window)
    PostMessageW((HWND)receiver->window, WM_CLOSE, 0, 0);
  if (child.hProcess) {
    if (WaitForSingleObject(child.hProcess, 2000) == WAIT_TIMEOUT) {
      TerminateProcess(child.hProcess, 2);
      WaitForSingleObject(child.hProcess, 1000);
    }
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    child.hProcess = NULL;
  }
  if (receiver)
    UnmapViewOfFile(receiver);
  receiver = NULL;
  if (mapping)
    CloseHandle(mapping);
  mapping = NULL;
  bool restored = restore_desktop();
  desktop_started = false;
  if (watchdog) {
    WaitForSingleObject(watchdog, 1000);
    CloseHandle(watchdog);
  }
  if (watchdog_stop)
    CloseHandle(watchdog_stop);
  if (!restored)
    ExitProcess(3);
}
static DWORD WINAPI timeout_cleanup(void *unused) {
  (void)unused;
  if (WaitForSingleObject(watchdog_stop, 60000) != WAIT_TIMEOUT)
    return 0;
  /* Last-resort owner timeout: OS teardown frees this process's hooks and
   * windows. Restore input/focus first, then terminate only our receiver. */
  fputs("FAIL sprite_flow hard timeout; terminating owned harness now\n",
        stderr);
  pp_platform_release_capture();
  if (receiver && receiver->window)
    PostMessageW((HWND)receiver->window, WM_CLOSE, 0, 0);
  if (child.hProcess &&
      WaitForSingleObject(child.hProcess, 1000) == WAIT_TIMEOUT)
    TerminateProcess(child.hProcess, 2);
  restore_desktop();
  fflush(stderr);
  TerminateProcess(GetCurrentProcess(), 2);
  return 0;
}
static void present_runtime(void) {
  if (!flow)
    return;
  if (simulated_count) {
    PPVec2 pointer;
    PPError error = {0};
    CHECK(pp_platform_pointer(&pointer));
    CHECK(pp_anchor_presenter_update(flow->presenter, simulated,
                                     simulated_count, pointer, now(), &error));
  } else
    pp_runtime_tick(flow, now());
  CHECK(!flow->failed);
}
static void pump(unsigned duration) {
  ULONGLONG end = GetTickCount64() + duration;
  do {
    present_runtime();
    PPPlatformEvent event;
    while (pp_ui_poll(&event)) {
      if (flow_settings)
        pp_settings_event(flow_settings, &event);
      pp_runtime_event(flow, &event);
    }
    if (flow_settings)
      pp_settings_draw(flow_settings);
    while (pp_platform_poll(&event)) {
      CHECK(event.kind != PP_EVENT_QUIT);
      pp_runtime_event(flow, &event);
      present_runtime();
    }
    pp_runtime_draw_ui(flow, now());
    Sleep(1);
  } while (GetTickCount64() < end);
}
typedef struct Injected {
  INPUT inputs[64];
  UINT count, sent;
} Injected;
static DWORD WINAPI inject(void *argument) {
  Injected *i = argument;
  i->sent = SendInput(i->count, i->inputs, sizeof(INPUT));
  return 0;
}
static void send_inputs(Injected *i) {
  HANDLE thread = CreateThread(NULL, 0, inject, i, 0, NULL);
  CHECK(thread);
  ULONGLONG end = GetTickCount64() + 3000;
  while (WaitForSingleObject(thread, 0) == WAIT_TIMEOUT &&
         GetTickCount64() < end)
    pump(1);
  CHECK(WaitForSingleObject(thread, 0) == WAIT_OBJECT_0);
  CloseHandle(thread);
  CHECK(i->sent == i->count);
  pump(20);
}
static void move(int x, int y) {
  Injected i = {.count = 1};
  i.inputs[0].type = INPUT_MOUSE;
  i.inputs[0].mi.dwFlags =
      MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
  i.inputs[0].mi.dx = MulDiv(x - GetSystemMetrics(SM_XVIRTUALSCREEN), 65535,
                             GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1);
  i.inputs[0].mi.dy = MulDiv(y - GetSystemMetrics(SM_YVIRTUALSCREEN), 65535,
                             GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1);
  send_inputs(&i);
  POINT actual;
  CHECK(GetCursorPos(&actual));
  CHECK(abs(actual.x - x) <= 1 && abs(actual.y - y) <= 1);
}
static void mouse(DWORD flags, DWORD data) {
  if (flags & MOUSEEVENTF_LEFTDOWN)
    left_held = true;
  if (flags & MOUSEEVENTF_RIGHTDOWN)
    right_held = true;
  Injected i = {.count = 1};
  i.inputs[0].type = INPUT_MOUSE;
  i.inputs[0].mi.dwFlags = flags;
  i.inputs[0].mi.mouseData = data;
  send_inputs(&i);
  if (flags & MOUSEEVENTF_LEFTUP)
    left_held = false;
  if (flags & MOUSEEVENTF_RIGHTUP)
    right_held = false;
}
static INPUT key_input(WORD key, bool down) {
  INPUT i = {.type = INPUT_KEYBOARD};
  i.ki.wScan = (WORD)MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
  i.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
  if (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN ||
      key == VK_HOME || key == VK_END)
    i.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  return i;
}
static void key(WORD key, bool down) {
  if (key == VK_MENU && down)
    alt_held = true;
  if (key == VK_CONTROL && down)
    control_held = true;
  Injected i = {.count = 1};
  i.inputs[0] = key_input(key, down);
  send_inputs(&i);
  if (key == VK_MENU && !down)
    alt_held = false;
  if (key == VK_CONTROL && !down)
    control_held = false;
}
static void click(int x, int y) {
  move(x, y);
  mouse(MOUSEEVENTF_LEFTDOWN, 0);
  mouse(MOUSEEVENTF_LEFTUP, 0);
}
static PPSurface *native_sprite(uint64_t id) {
  PPSprite *s = pp_sprites_find(flow->presenter, id);
  CHECK(s);
  return s->surface;
}
static POINT opaque_point(uint64_t id) {
  PPSurface *s = native_sprite(id);
  for (int y = 2; y < s->height - 2; y++)
    for (int x = 2; x < s->width - 2; x++)
      if (s->bits[((size_t)y * s->width + x) * 4 + 3] > 240 &&
          s->bits[((size_t)(y + 1) * s->width + x + 1) * 4 + 3] > 240)
        return (POINT){s->origin.x + x, s->origin.y + y};
  CHECK(false);
  return (POINT){0};
}
static uint64_t pull(int x, int y) {
  size_t before = pp_scene_count(pp_app_scene(flow_app));
  move(8, 8);
  pump(25);
  mouse(MOUSEEVENTF_LEFTDOWN, 0);
  move(9, 9);
  CHECK(pp_scene_count(pp_app_scene(flow_app)) == before);
  move(x, y);
  CHECK(pp_scene_count(pp_app_scene(flow_app)) == before + 1);
  mouse(MOUSEEVENTF_LEFTUP, 0);
  CHECK(!pp_windows_input_owner());
  return pp_scene_at(pp_app_scene(flow_app), before)->id;
}
static void arrange(uint64_t id, double x, double y) {
  PPError error = {0};
  PPPose pose = pp_scene_get(pp_app_scene(flow_app), id)->pose;
  pose.center = (PPVec2){x, y};
  CHECK(pp_scene_set_pose(pp_app_scene(flow_app), id, pose, &error));
  pump(20);
}
static void open_drawer(uint64_t id) {
  POINT point = opaque_point(id);
  pump(550);
  click(point.x, point.y);
  click(point.x, point.y);
  pump(60);
  CHECK(flow->drawer && flow->drawer->instance_id == id &&
        flow->drawer->backend->visible);
  HWND window = SDL_GetPointerProperty(
      SDL_GetWindowProperties(flow->drawer->backend->window),
      SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
  CHECK(GetForegroundWindow() == window);
}
static void ui_click(PPUIBackend *ui, const char *id) {
  PPRect r;
  CHECK(pp_ui_control_rect(ui, id, &r));
  HWND window =
      SDL_GetPointerProperty(SDL_GetWindowProperties(ui->window),
                             SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
  POINT point = {(int)((r.x + r.w / 2) * ui->scale),
                 (int)((r.y + r.h / 2) * ui->scale)};
  CHECK(ClientToScreen(window, &point));
  click(point.x, point.y);
}
static uint64_t add_image(const char *svg) {
  PPError error = {0};
  PPItem item = {.kind = PP_ITEM_IMAGE, .image_longest_dip = 96, .opacity = 1};
  CHECK(pp_asset_import(pp_app_assets(flow_app),
                        (PPBytes){(const uint8_t *)svg, strlen(svg)},
                        PP_ASSET_SVG, &item.image_asset, &error));
  CHECK(pp_app_command(flow_app,
                       &(PPCommand){.kind = PP_COMMAND_ADD_ITEM,
                                    .anchor = PP_ANCHOR_TOP_LEFT,
                                    .item = &item},
                       &error));
  return pp_app_ui(flow_app)->selected_item;
}
static void check_dismissed(const char *stage) {
  HWND foreground = GetForegroundWindow();
  printf("Drawer %s: dismissed=%s foreground=%p receiver=%p receiver-focus=%s "
         "clicks=%ld capture=%p\n",
         stage, flow->drawer ? "no" : "yes", (void *)foreground,
         (void *)receiver->window,
         foreground == (HWND)receiver->window ? "yes" : "no", receiver->clicks,
         (void *)pp_windows_input_owner());
  fflush(stdout);
  CHECK(!flow->drawer);
  CHECK(foreground == (HWND)receiver->window);
}
static POINT outside_receiver_point(void) {
  RECT client;
  HWND window = (HWND)receiver->window;
  CHECK(GetClientRect(window, &client));
  POINT origin = {0};
  CHECK(ClientToScreen(window, &origin));
  /* Use actual native client bounds. A fixed screen point can fall inside
   * the enlarged drawer at 150/200% or outside the receiver after clipping. */
  for (LONG y = client.bottom - 40; y > client.top + 40; y -= 80)
    for (LONG x = client.left + 40; x < client.right - 40; x += 80) {
      POINT point = {origin.x + x, origin.y + y};
      if (WindowFromPoint(point) != window)
        continue;
      move(point.x, point.y);
      /* Hover may reveal an anchor. Recheck before delivering any button. */
      if (WindowFromPoint(point) == window)
        return point;
    }
  CHECK(false);
  return (POINT){0};
}
static void trace_hole_target(const char *stage) {
  HWND hit=WindowFromPoint((POINT){500,380}); DWORD pid=0;
  DWORD thread=GetWindowThreadProcessId(hit,&pid);
  GUITHREADINFO gui={sizeof(gui)}; GetGUIThreadInfo(thread,&gui);
  wchar_t type[128]={0},image[MAX_PATH]={0}; GetClassNameW(hit,type,128);
  HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
  if(process) { DWORD size=MAX_PATH; QueryFullProcessImageNameW(process,0,image,&size); CloseHandle(process); }
  printf("HOLE TARGET %s hwnd=%p class=%ls pid=%lu tid=%lu process=%ls owner=%p root=%p menu-owner=%p gui-flags=%lu foreground=%p receiver=%p receiver-pid=%lu\n",
    stage,(void *)hit,type,pid,thread,image,(void *)GetWindow(hit,GW_OWNER),(void *)GetAncestor(hit,GA_ROOTOWNER),(void *)gui.hwndMenuOwner,gui.flags,(void *)GetForegroundWindow(),(void *)receiver->window,child.dwProcessId);
  fflush(stdout);
}
static BOOL CALLBACK cancel_transient_menu(HWND window,LPARAM context) {
  unsigned *cancelled=(unsigned *)context; wchar_t type[128]={0};
  GetClassNameW(window,type,128);
  if(wcscmp(type,L"#32768")) return TRUE;
  DWORD pid=0,thread=GetWindowThreadProcessId(window,&pid);
  GUITHREADINFO gui={sizeof(gui)};
  if(!GetGUIThreadInfo(thread,&gui) || !(gui.flags&GUI_INMENUMODE) || !gui.hwndMenuOwner) return TRUE;
  CHECK(*cancelled<8); (*cancelled)++;
  printf("PREFLIGHT cancel transient native menu hwnd=%p pid=%lu owner=%p flags=%lu\n",(void *)window,pid,(void *)gui.hwndMenuOwner,gui.flags);
  fflush(stdout); DWORD_PTR response=0;
  CHECK(SendMessageTimeoutW(gui.hwndMenuOwner,WM_CANCELMODE,0,0,SMTO_ABORTIFHUNG,1000,&response));
  return TRUE;
}
static void prepare_receiver_target(void) {
  /* A shell menu may retain modal state while hidden behind foreground UI.
   * Inspect only native menu windows and cancel their actual active owner. */
  unsigned cancelled=0; CHECK(EnumWindows(cancel_transient_menu,(LPARAM)&cancelled));
  POINT point={500,380}; HWND hit=WindowFromPoint(point);
  wchar_t type[128]={0}; GetClassNameW(hit,type,128);
  if(!wcscmp(type,L"#32768")) {
    trace_hole_target("preflight-obstructing-menu");
    DWORD thread=GetWindowThreadProcessId(hit,NULL);
    GUITHREADINFO gui={sizeof(gui)}; CHECK(GetGUIThreadInfo(thread,&gui));
    CHECK(gui.hwndMenuOwner && (gui.flags&GUI_INMENUMODE));
    /* Cancel only the observed transient menu. Do not close its owner or
     * terminate a foreign process. The receiver must then own this point. */
    DWORD_PTR response=0;
    CHECK(SendMessageTimeoutW(gui.hwndMenuOwner,WM_CANCELMODE,0,0,SMTO_ABORTIFHUNG,1000,&response));
    ULONGLONG end=GetTickCount64()+500;
    while(WindowFromPoint(point)!=(HWND)receiver->window && GetTickCount64()<end) Sleep(10);
    trace_hole_target("preflight-menu-cancelled");
  }
  CHECK(WindowFromPoint(point)==(HWND)receiver->window);
}
static void test_desktop_flow(void) {
  PPError error = {0};
  CHECK(pp_app_create(&flow_app, &(PPAppOptions){.preferences_directory = ""},
                      &error));
  for (int a = 1; a < 8; a++)
    CHECK(pp_app_command(flow_app,
                         &(PPCommand){.kind = PP_COMMAND_SET_ENABLED,
                                      .anchor = (PPAnchor)a,
                                      .enabled = false},
                         &error));
  CHECK(pp_runtime_create(&flow, flow_app, &error));
  pump(50);
  uint64_t hand = pull(320, 260);
  trace_hole_target("after-hand-pull");
  PPSurface *hand_surface = native_sprite(hand);
  size_t yellow = 0;
  for (int y = 0; y < hand_surface->height; y++)
    for (int x = 0; x < hand_surface->width; x++) {
      uint8_t *p =
          hand_surface->bits + ((size_t)y * hand_surface->width + x) * 4;
      if (p[2] > 170 && p[1] > 95 && p[0] < 90 && p[3] > 200)
        yellow++;
    }
  CHECK(yellow > 25 && GetForegroundWindow() == (HWND)receiver->window);
  SDL_Surface *hand_proof = SDL_CreateSurfaceFrom(
      hand_surface->width, hand_surface->height, SDL_PIXELFORMAT_BGRA32,
      hand_surface->bits, hand_surface->width * 4);
  CHECK(hand_proof);
  CHECK(SDL_SaveBMP(hand_proof, PP_FLOW_OUTPUT "/sprite-native-hand.bmp"));
  SDL_DestroySurface(hand_proof);
  uint64_t ring = add_image(
      "<svg width='100' height='100'><path fill='blue' fill-rule='evenodd' "
      "d='M0 0H100V100H0Z M30 30V70H70V30Z'/></svg>");
  CHECK(pp_app_command(flow_app,
                       &(PPCommand){.kind = PP_COMMAND_SET_DEFAULT,
                                    .anchor = PP_ANCHOR_TOP_LEFT,
                                    .item_id = ring},
                       &error));
  uint64_t a = pull(500, 300);
  trace_hole_target("after-first-ring-pull");
  uint64_t b = pull(820, 300);
  trace_hole_target("after-second-ring-pull");
  arrange(a, 500, 380);
  arrange(b, 800, 380);
  CHECK(pp_item_snapshot_item(
            pp_scene_get(pp_app_scene(flow_app), hand)->snapshot)
            ->kind == PP_ITEM_TEXT);
  LONG clicks = receiver->clicks, wheels = receiver->wheels;
  trace_hole_target("before-hole-input");
  CHECK(WindowFromPoint((POINT){500,380})==(HWND)receiver->window);
  click(500, 380);
  LONG clicks_after_hole=receiver->clicks;
  mouse(MOUSEEVENTF_WHEEL, WHEEL_DELTA);
  if(receiver->clicks != clicks+1 || receiver->wheels != wheels+1) {
    POINT actual; GetCursorPos(&actual); RECT client,screen;
    GetClientRect((HWND)receiver->window,&client); GetWindowRect((HWND)receiver->window,&screen);
    HWND hit=WindowFromPoint(actual); wchar_t hit_class[128]={0}; GetClassNameW(hit,hit_class,128);
    PPSurface *surface=native_sprite(a);
    int px=500-surface->origin.x,py=380-surface->origin.y;
    int alpha=px>=0 && py>=0 && px<surface->width && py<surface->height ? surface->bits[((size_t)py*surface->width+px)*4+3] : -1;
    DWORD_PTR response=0;
    LRESULT responsive=SendMessageTimeoutW((HWND)receiver->window,WM_NULL,0,0,SMTO_ABORTIFHUNG,1000,&response);
    printf("FAIL passthrough expected-clicks=%ld actual-clicks=%ld expected-wheels=%ld actual-wheels=%ld cursor=%ld,%ld hit=%p class=%ls receiver=%p foreground=%p receiver-rect=%ld,%ld,%ld,%ld client=%ld,%ld ring-alpha=%d owner=%p\n",
      clicks+1,receiver->clicks,wheels+1,receiver->wheels,actual.x,actual.y,(void *)hit,hit_class,(void *)receiver->window,(void *)GetForegroundWindow(),screen.left,screen.top,screen.right,screen.bottom,client.right,client.bottom,alpha,(void *)pp_windows_input_owner());
    printf("Receiver acknowledgement=%s clicks-immediately-after-hole=%ld\n",responsive?"responsive":"FAILED",clicks_after_hole);
    fflush(stdout);
  }
  CHECK(receiver->clicks == clicks + 1 && receiver->wheels == wheels + 1);
  POINT point = opaque_point(a);
  move(point.x, point.y);
  mouse(MOUSEEVENTF_LEFTDOWN, 0);
  move(1100, 750);
  CHECK(pp_windows_input_owner() == native_sprite(a));
  mouse(MOUSEEVENTF_LEFTUP, 0);
  CHECK(!pp_windows_input_owner() &&
        GetForegroundWindow() == (HWND)receiver->window);
  arrange(a, 500, 380);
  point = opaque_point(a);
  move(point.x, point.y);
  double angle = pp_scene_get(pp_app_scene(flow_app), a)->pose.angle_deg;
  mouse(MOUSEEVENTF_WHEEL, WHEEL_DELTA);
  pump(150);
  CHECK(fabs(pp_scene_get(pp_app_scene(flow_app), a)->pose.angle_deg - angle -
             10) < .01);
  point = opaque_point(a);
  move(point.x, point.y);
  double longest = pp_scene_get(pp_app_scene(flow_app), a)->pose.longest_dip;
  key(VK_MENU, true);
  mouse(MOUSEEVENTF_WHEEL, WHEEL_DELTA);
  key(VK_MENU, false);
  pump(150);
  CHECK(fabs(pp_scene_get(pp_app_scene(flow_app), a)->pose.longest_dip -
             longest * 1.1) < .01);
  PPPose before = pp_scene_get(pp_app_scene(flow_app), a)->pose;
  uint64_t revision = pp_project_revision(pp_app_project(flow_app));
  open_drawer(a);
  CHECK(!memcmp(&before, &pp_scene_get(pp_app_scene(flow_app), a)->pose,
                sizeof(before)));
  flow->drawer->backend->capture_path =
      PP_FLOW_OUTPUT "/drawer-native-mixed.bmp";
  pump(20);
  key(VK_ESCAPE, true);
  key(VK_ESCAPE, false);
  check_dismissed("Escape");
  open_drawer(a);
  POINT outside = outside_receiver_point();
  HWND drawer_window = SDL_GetPointerProperty(
      SDL_GetWindowProperties(flow->drawer->backend->window),
      SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
  RECT drawer_rect;
  CHECK(GetWindowRect(drawer_window, &drawer_rect));
  printf("Outside point=(%ld,%ld) target=%p receiver=%p drawer=%p "
         "rect=(%ld,%ld,%ld,%ld) scale=%.2f\n",
         outside.x, outside.y, (void *)WindowFromPoint(outside),
         (void *)receiver->window, (void *)drawer_window, drawer_rect.left,
         drawer_rect.top, drawer_rect.right, drawer_rect.bottom,
         flow->drawer->scale);
  fflush(stdout);
  CHECK(WindowFromPoint(outside) == (HWND)receiver->window);
  clicks = receiver->clicks;
  mouse(MOUSEEVENTF_LEFTDOWN, 0);
  mouse(MOUSEEVENTF_LEFTUP, 0);
  DWORD_PTR response;
  CHECK(SendMessageTimeoutW((HWND)receiver->window, WM_NULL, 0, 0,
                            SMTO_ABORTIFHUNG, 1000, &response));
  check_dismissed("outside click");
  CHECK(receiver->clicks == clicks + 1);
  open_drawer(a);
  char control[64];
  snprintf(control, sizeof(control), "drawer-item-%llu",
           (unsigned long long)ring);
  ui_click(flow->drawer->backend, control);
  check_dismissed("selection");
  CHECK(pp_project_revision(pp_app_project(flow_app)) == revision &&
        !memcmp(&before, &pp_scene_get(pp_app_scene(flow_app), a)->pose,
                sizeof(before)));
  puts("PASS real hand, repeated default pulls, transparent click/wheel, "
       "cross-window capture, eased wheel/Alt wheel, stationary double-click, "
       "drawer selection/Escape/outside and presentation focus");
  /* Replace the lower sprite with an actual GIF; advancing its frame must
   * not reorder it above the static sprite that the presenter raised. */
  FILE *file = fopen(PP_MEDIA_FIXTURES "/changing-alpha.gif", "rb");
  CHECK(file);
  CHECK(!fseek(file, 0, SEEK_END));
  long n = ftell(file);
  rewind(file);
  uint8_t *bytes = malloc((size_t)n);
  CHECK(bytes && fread(bytes, 1, (size_t)n, file) == (size_t)n);
  fclose(file);
  PPItem gif = {.kind = PP_ITEM_IMAGE, .image_longest_dip = 96, .opacity = 1};
  CHECK(pp_asset_import(pp_app_assets(flow_app), (PPBytes){bytes, (size_t)n},
                        PP_ASSET_GIF, &gif.image_asset, &error));
  free(bytes);
  frozen_time =
      (double)SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency();
  CHECK(pp_scene_replace(pp_app_scene(flow_app), b, &gif, frozen_time, &error));
  arrange(a, 650, 450);
  arrange(b, 650, 450);
  point = opaque_point(a);
  /* Raise via a real click on a temporarily exposed part of a. */
  arrange(b, 950, 450);
  point = opaque_point(a);
  click(point.x, point.y);
  arrange(b, 650, 450);
  CHECK(WindowFromPoint(point) == native_sprite(a)->window);
  frozen_time += .11;
  pump(30);
  CHECK(WindowFromPoint(point) == native_sprite(a)->window);
  puts("PASS lower GIF frame presentation preserves raised static sprite "
       "ordering");
  arrange(b, 900, 600);
  double start = now();
  CHECK(pp_scene_replace(pp_app_scene(flow_app), b, &gif, start, &error));
  pump(20);
  PPSurface *gs = native_sprite(b);
  POINT stationary = {gs->origin.x + gs->width / 2,
                      gs->origin.y + gs->height / 6};
  move(stationary.x, stationary.y);
  CHECK(WindowFromPoint(stationary) == gs->window);
  clicks = receiver->clicks;
  wheels = receiver->wheels;
  frozen_time = start + .11;
  pump(25);
  POINT actual;
  CHECK(GetCursorPos(&actual));
  CHECK(actual.x == stationary.x && actual.y == stationary.y);
  CHECK(WindowFromPoint(stationary) == (HWND)receiver->window);
  mouse(MOUSEEVENTF_LEFTDOWN, 0);
  mouse(MOUSEEVENTF_LEFTUP, 0);
  mouse(MOUSEEVENTF_WHEEL, WHEEL_DELTA);
  CHECK(receiver->clicks == clicks + 1 && receiver->wheels == wheels + 1);
  frozen_time = 0;
  puts("PASS stationary pointer across actual GIF transparent frame routes "
       "original click/wheel to lower process");
  point = opaque_point(a);
  move(point.x, point.y);
  mouse(MOUSEEVENTF_LEFTDOWN, 0);
  move(point.x + 20, point.y + 20);
  CHECK(pp_windows_input_owner());
  simulated[0] = (PPDisplay){UINT64_C(9001), {100, 100, 900, 700}, 1, 1};
  simulated_count = 1;
  pump(25);
  CHECK(!pp_windows_input_owner());
  const PPSceneInstance *recovered = pp_scene_get(pp_app_scene(flow_app), a);
  CHECK(recovered->pose.center.x >= 100 && recovered->pose.center.x <= 1000);
  mouse(MOUSEEVENTF_LEFTUP, 0);
  simulated_count = 0;
  pump(20);
  point = opaque_point(a);
  move(point.x, point.y);
  HWND deleted_window = native_sprite(a)->window;
  LONG right_downs = receiver->right_clicks, right_ups = receiver->right_ups;
  LONG contexts = receiver->context_menus;
  size_t before_delete = pp_scene_count(pp_app_scene(flow_app));
  CHECK(GetForegroundWindow() == (HWND)receiver->window);
  mouse(MOUSEEVENTF_RIGHTDOWN, 0);
  CHECK(!pp_scene_get(pp_app_scene(flow_app), a) && !IsWindow(deleted_window));
  CHECK(pp_scene_count(pp_app_scene(flow_app)) == before_delete-1);
  printf("right deletion boundary: scene absent, window destroyed before physical up; receiver down=%ld up=%ld context=%ld\n",
         receiver->right_clicks-right_downs,receiver->right_ups-right_ups,receiver->context_menus-contexts);
  mouse(MOUSEEVENTF_RIGHTUP, 0);
  printf("right deletion release: receiver down=%ld up=%ld context=%ld\n",
         receiver->right_clicks-right_downs,receiver->right_ups-right_ups,receiver->context_menus-contexts);
  fflush(stdout);
  CHECK(receiver->right_clicks == right_downs && receiver->right_ups == right_ups && receiver->context_menus == contexts);
  puts("PASS simulated monitor removal during native drag releases capture and "
       "keeps sprite reachable; right-click releases instance");
  CHECK(pp_settings_create_scaled(&flow_settings, flow_app, 1, &error));
  pump(60);
  HWND settings_window = SDL_GetPointerProperty(
      SDL_GetWindowProperties(flow_settings->backend->window),
      SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
  CHECK(SetForegroundWindow(settings_window));
  pump(25);
  uint64_t covered = pull(220, 200);
  point = opaque_point(covered);
  CHECK(WindowFromPoint(point) ==
        settings_window); /* Newly created sprite remains below settings. */
  ui_click(flow_settings->backend, "project-name");
  key(VK_CONTROL, true);
  key('A', true);
  key('A', false);
  key(VK_CONTROL, false);
  uint64_t generation = pp_app_generation(flow_app);
  Injected batch = {0};
  const wchar_t *name = L"Flow edit";
  for (const wchar_t *p = name; *p; p++) {
    INPUT down = {.type = INPUT_KEYBOARD};
    down.ki.wScan = *p;
    down.ki.dwFlags = KEYEVENTF_UNICODE;
    batch.inputs[batch.count++] = down;
    down.ki.dwFlags |= KEYEVENTF_KEYUP;
    batch.inputs[batch.count++] = down;
  }
  batch.inputs[batch.count++] = key_input(VK_CONTROL, true);
  batch.inputs[batch.count++] = key_input('N', true);
  batch.inputs[batch.count++] = key_input('N', false);
  batch.inputs[batch.count++] = key_input(VK_CONTROL, false);
  control_held = true;
  send_inputs(&batch);
  control_held = false;
  pump(50);
  CHECK(!strcmp(pp_app_name(flow_app), "Flow edit") &&
        pp_app_ui(flow_app)->transition_pending &&
        pp_app_generation(flow_app) == generation);
  ui_click(flow_settings->backend, "cancel-transition");
  CHECK(!pp_app_ui(flow_app)->transition_pending);
  ui_click(flow_settings->backend, "close-settings");
  CHECK(!pp_app_ui(flow_app)->settings_open);
  CHECK(SetForegroundWindow((HWND)receiver->window));
  puts("PASS settings priority and same native input batch commits field edit "
       "before New in the combined runtime/UI loop");
}
static void desktop_main(const char *receiver_path) {
  CHECK(!(GetAsyncKeyState(VK_LBUTTON) & 0x8000) &&
        !(GetAsyncKeyState(VK_RBUTTON) & 0x8000) &&
        !(GetAsyncKeyState(VK_MENU) & 0x8000) &&
        !(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
        !(GetAsyncKeyState(VK_ESCAPE) & 0x8000) &&
        !(GetAsyncKeyState('A') & 0x8000) && !(GetAsyncKeyState('N') & 0x8000));
  CHECK(GetCursorPos(&old_cursor));
  old_foreground = GetForegroundWindow();
  desktop_started = true;
  atexit(cleanup);
  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  watchdog_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
  CHECK(watchdog_stop);
  watchdog = CreateThread(NULL, 0, timeout_cleanup, NULL, 0, NULL);
  CHECK(watchdog);
  char name[96];
  snprintf(name, sizeof(name), "Local\\PointySpriteFlow-%lu",
           GetCurrentProcessId());
  mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                               sizeof(ReceiverState), name);
  CHECK(mapping);
  receiver =
      MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*receiver));
  CHECK(receiver);
  char command[2048];
  snprintf(command, sizeof(command), "\"%s\" %s", receiver_path, name);
  STARTUPINFOA startup = {sizeof(startup)};
  CHECK(CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL,
                       &startup, &child));
  ULONGLONG end = GetTickCount64() + 5000;
  while (!receiver->window && GetTickCount64() < end)
    Sleep(10);
  CHECK(receiver->window);
  CHECK(SetWindowPos((HWND)receiver->window, NULL, -8, -32, 1450, 1050,
                     SWP_NOZORDER));
  CHECK(SetForegroundWindow((HWND)receiver->window));
  prepare_receiver_target();
  test_desktop_flow();
  cleanup();
  puts("PASS sprite_flow desktop integration");
}
int main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "--hidden")) {
    hidden_drawer();
    hidden_long_list();
    return 0;
  }
  CHECK(argc == 2);
  desktop_main(argv[1]);
  return 0;
}
