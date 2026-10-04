#include "../../src/platform/windows/windows_internal.h"
#include "../../src/ui/settings_internal.h"
#include "../test.h"
#include "pointy/project_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static PPApp *app;
static PPSettings *settings;
static PPSurface *overlap;
static PPError error;
static POINT original_cursor;
static HWND original_foreground;
static bool desktop_run;
static bool dialog_diagnostic;
static bool manual_font;
static unsigned sprite_presses;
static unsigned normalized_down_keys;
static char test_directory[MAX_PATH], package[MAX_PATH], preferences[MAX_PATH];
static bool restore_foreground(const char *stage) {
  HWND before = GetForegroundWindow();
  DWORD target_process = 0, current_process = 0;
  GetWindowThreadProcessId(original_foreground, &target_process);
  GetWindowThreadProcessId(before, &current_process);
  bool valid = original_foreground && IsWindow(original_foreground);
  BOOL requested = valid ? SetForegroundWindow(original_foreground) : FALSE;
  DWORD_PTR result = 0;
  /* Activation crosses a thread queue. WM_NULL confirms the target serviced
   * the request before we verify focus; this is not a timing sleep. */
  BOOL responsive =
      valid ? (BOOL)SendMessageTimeoutW(original_foreground, WM_NULL, 0, 0,
                                        SMTO_ABORTIFHUNG, 1000, &result)
            : FALSE;
  bool restored = valid && GetForegroundWindow() == original_foreground;
  fprintf(stderr,
          "Foreground %s: original=%p valid=%d pid=%lu before=%p pid=%lu "
          "requested=%d responsive=%d after=%p restored=%d\n",
          stage, (void *)original_foreground, valid, target_process,
          (void *)before, current_process, requested, responsive,
          (void *)GetForegroundWindow(), restored);
  return restored;
}
static void cleanup(void) {
  pp_platform_release_capture();
  pp_surface_destroy(overlap);
  overlap = NULL;
  UINT released = 0;
  if (desktop_run) {
    INPUT release[6] = {0};
    release[0].type = INPUT_MOUSE;
    release[0].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    const WORD keys[] = {VK_CONTROL, VK_SHIFT, VK_MENU, VK_RETURN, VK_TAB};
    for (int i = 0; i < 5; i++) {
      release[i + 1].type = INPUT_KEYBOARD;
      release[i + 1].ki.wVk = keys[i];
      release[i + 1].ki.dwFlags = KEYEVENTF_KEYUP;
    }
    released = SendInput(6, release, sizeof(INPUT));
    SetPhysicalCursorPos(original_cursor.x, original_cursor.y);
  }
  bool restored = false;
  if (desktop_run || manual_font)
    restored = restore_foreground("before destroying owned windows");
  pp_settings_destroy(settings);
  settings = NULL;
  pp_app_destroy(app);
  app = NULL;
  if ((desktop_run || manual_font) &&
      GetForegroundWindow() != original_foreground)
    restored = restore_foreground("after destroying owned windows");
  if (desktop_run) {
    POINT restored_cursor = {0};
    bool cursor_restored = GetPhysicalCursorPos(&restored_cursor) &&
                           restored_cursor.x == original_cursor.x &&
                           restored_cursor.y == original_cursor.y;
    fprintf(stderr,
            "Cleanup: released=%u/6, physical cursor restored=%d, foreground "
            "restored=%d\n",
            released, cursor_restored, restored);
  } else if (manual_font) {
    fprintf(stderr,
            "Manual cleanup: owned windows closed, foreground restored=%d; no "
            "keyboard/pointer input synthesized.\n",
            restored);
  }
  if (*package)
    pp_platform_remove_file(package, NULL);
  if (*preferences)
    pp_platform_remove_file(preferences, NULL);
  if (*test_directory)
    RemoveDirectoryA(test_directory);
}
static void pump(unsigned duration) {
  ULONGLONG until = GetTickCount64() + duration;
  do {
    PPPlatformEvent event;
    while (pp_ui_poll(&event)) {
      if (event.kind == PP_EVENT_KEY_DOWN && event.key == PP_KEY_DOWN)
        normalized_down_keys++;
      pp_settings_event(settings, &event);
    }
    while (pp_platform_poll(&event))
      if (event.kind == PP_EVENT_BUTTON_DOWN && event.surface == overlap)
        sprite_presses++;
    pp_settings_draw(settings);
    CHECK(!settings->failed);
    SDL_Delay(2);
  } while (GetTickCount64() < until);
}
typedef struct Injection {
  INPUT *inputs;
  UINT count, sent;
} Injection;
static DWORD WINAPI inject(void *context) {
  Injection *value = context;
  value->sent = SendInput(value->count, value->inputs, sizeof(INPUT));
  return 0;
}
static void send_inputs(INPUT *inputs, UINT count) {
  CHECK(desktop_run);
  Injection value = {inputs, count, 0};
  HANDLE thread = CreateThread(NULL, 0, inject, &value, 0, NULL);
  CHECK(thread);
  ULONGLONG deadline = GetTickCount64() + 3000;
  while (WaitForSingleObject(thread, 0) == WAIT_TIMEOUT &&
         GetTickCount64() < deadline)
    pump(1);
  CHECK(WaitForSingleObject(thread, 0) == WAIT_OBJECT_0);
  CloseHandle(thread);
  CHECK(value.sent == count);
  pump(25);
}
static void key_inputs(WORD code, INPUT inputs[2]) {
  memset(inputs, 0, 2 * sizeof(*inputs));
  inputs[0].type = inputs[1].type = INPUT_KEYBOARD;
  UINT scan = MapVirtualKeyW(code, MAPVK_VK_TO_VSC_EX);
  CHECK(scan);
  inputs[0].ki.wScan = inputs[1].ki.wScan = (WORD)(scan & 0xff);
  /* MapVirtualKey returns 0x50 for VK_DOWN on this host, so logical navigation
   * keys require an explicit E0 flag. Without it SDL sees keypad scancodes. */
  bool extended = (scan & 0xff00) == 0xe000 ||
                  (code >= VK_PRIOR && code <= VK_DOWN) || code == VK_INSERT ||
                  code == VK_DELETE;
  DWORD flags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
  inputs[0].ki.dwFlags = flags;
  inputs[1].ki.dwFlags = flags | KEYEVENTF_KEYUP;
}
static void key(WORD code) {
  INPUT inputs[2];
  key_inputs(code, inputs);
  send_inputs(inputs, 2);
}
static void chord(WORD modifier, WORD code) {
  INPUT inputs[4] = {0};
  for (int i = 0; i < 4; i++)
    inputs[i].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = inputs[3].ki.wVk = modifier;
  inputs[1].ki.wVk = inputs[2].ki.wVk = code;
  inputs[2].ki.dwFlags = inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
  send_inputs(inputs, 4);
}
static void type(const wchar_t *text) {
  size_t length = wcslen(text);
  INPUT *inputs = calloc(length * 2, sizeof(*inputs));
  CHECK(inputs);
  for (size_t i = 0; i < length; i++) {
    inputs[i * 2].type = inputs[i * 2 + 1].type = INPUT_KEYBOARD;
    inputs[i * 2].ki.wScan = inputs[i * 2 + 1].ki.wScan = text[i];
    inputs[i * 2].ki.dwFlags = KEYEVENTF_UNICODE;
    inputs[i * 2 + 1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
  }
  send_inputs(inputs, (UINT)(length * 2));
  free(inputs);
}
static HWND window(void) {
  return SDL_GetPointerProperty(
      SDL_GetWindowProperties(settings->backend->window),
      SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}
static void focus_control(const char *id) {
  for (int i = 0; i < 512; i++) {
    if (pp_ui_focused(settings->backend, id)) {
      pump(50);
      return;
    }
    key(VK_TAB);
  }
  fprintf(stderr, "Keyboard focus could not reach %s\n", id);
  CHECK(false);
}
static POINT control_center(const char *id) {
  PPRect bounds;
  CHECK(pp_ui_control_rect(settings->backend, id, &bounds));
  POINT point = {(LONG)((bounds.x + bounds.w / 2) * settings->backend->scale),
                 (LONG)((bounds.y + bounds.h / 2) * settings->backend->scale)};
  CHECK(ClientToScreen(window(), &point));
  return point;
}
static void click_control(const char *id) {
  focus_control(id);
  POINT point = control_center(id);
  RECT rect;
  CHECK(GetWindowRect(window(), &rect));
  CHECK(PtInRect(&rect, point));
  INPUT inputs[3] = {0};
  for (int i = 0; i < 3; i++)
    inputs[i].type = INPUT_MOUSE;
  inputs[0].mi.dwFlags =
      MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
  inputs[0].mi.dx = MulDiv(point.x - GetSystemMetrics(SM_XVIRTUALSCREEN), 65535,
                           GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1);
  inputs[0].mi.dy = MulDiv(point.y - GetSystemMetrics(SM_YVIRTUALSCREEN), 65535,
                           GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1);
  inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
  inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
  send_inputs(inputs, 3);
  POINT actual;
  CHECK(GetCursorPos(&actual));
  if (abs(actual.x - point.x) > 1 || abs(actual.y - point.y) > 1) {
    fprintf(stderr, "External pointer interference during %s\n", id);
    CHECK(false);
  }
}
static void replace_field(const char *id, const wchar_t *text) {
  click_control(id);
  chord(VK_CONTROL, 'A');
  type(text);
  pump(40);
}
static BOOL CALLBACK find_dialog(HWND candidate, LPARAM context) {
  DWORD process;
  GetWindowThreadProcessId(candidate, &process);
  char name[64];
  GetClassNameA(candidate, name, sizeof(name));
  if (process == GetCurrentProcessId() && IsWindowVisible(candidate) &&
      !strcmp(name, "#32770")) {
    *(HWND *)context = candidate;
    return FALSE;
  }
  return TRUE;
}
static HWND wait_dialog(void) {
  HWND dialog = NULL;
  ULONGLONG until = GetTickCount64() + 6000;
  do {
    pump(20);
    EnumWindows(find_dialog, (LPARAM)&dialog);
  } while (!dialog && GetTickCount64() < until);
  CHECK(dialog);
  return dialog;
}
static void describe_control(HWND control) {
  wchar_t class_name[80] = {0}, value[1024] = {0};
  GetClassNameW(control, class_name, 80);
  DWORD_PTR copied = 0;
  SendMessageTimeoutW(control, WM_GETTEXT, 1024, (LPARAM)value,
                      SMTO_ABORTIFHUNG, 500, &copied);
  char class_utf8[256] = {0}, value_utf8[4096] = {0};
  WideCharToMultiByte(CP_UTF8, 0, class_name, -1, class_utf8,
                      sizeof(class_utf8), NULL, NULL);
  WideCharToMultiByte(CP_UTF8, 0, value, -1, value_utf8, sizeof(value_utf8),
                      NULL, NULL);
  fprintf(stderr, "  HWND=%p ID=%d class=%s visible=%d enabled=%d text=%s\n",
          (void *)control, GetDlgCtrlID(control), class_utf8,
          IsWindowVisible(control), IsWindowEnabled(control), value_utf8);
}
static void capture_dialog(HWND dialog) {
  RECT rect;
  if (!GetWindowRect(dialog, &rect))
    return;
  int width = rect.right - rect.left, height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
    return;
  HDC window_dc = GetDC(dialog),
      memory = window_dc ? CreateCompatibleDC(window_dc) : NULL;
  BITMAPINFO info = {0};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void *pixels = NULL;
  HBITMAP bitmap =
      memory ? CreateDIBSection(memory, &info, DIB_RGB_COLORS, &pixels, NULL, 0)
             : NULL;
  if (bitmap) {
    HGDIOBJ previous = SelectObject(memory, bitmap);
    memset(pixels, 255, (size_t)width * height * 4);
    DWORD_PTR result = 0;
    if (SendMessageTimeoutW(dialog, WM_PRINT, (WPARAM)memory,
                            PRF_CLIENT | PRF_NONCLIENT | PRF_CHILDREN |
                                PRF_ERASEBKGND,
                            SMTO_ABORTIFHUNG, 500, &result)) {
      SDL_Surface *surface = SDL_CreateSurfaceFrom(
          width, height, SDL_PIXELFORMAT_BGRA32, pixels, width * 4);
      static unsigned sequence;
      char path[MAX_PATH];
      snprintf(path, sizeof(path), "%s/task8-native-dialog-%u.bmp",
               PP_SMOKE_OUTPUT, ++sequence);
      if (surface && SDL_SaveBMP(surface, path))
        fprintf(stderr, "  Owned-dialog capture: %s\n", path);
      else
        fprintf(stderr, "  Owned-dialog capture unavailable: %s\n",
                SDL_GetError());
      SDL_DestroySurface(surface);
    } else
      fprintf(stderr, "  Owned-dialog WM_PRINT timed out or failed (%lu)\n",
              GetLastError());
    SelectObject(memory, previous);
    DeleteObject(bitmap);
  }
  if (memory)
    DeleteDC(memory);
  if (window_dc)
    ReleaseDC(dialog, window_dc);
}
static BOOL CALLBACK describe_child(HWND control, LPARAM context) {
  (void)context;
  wchar_t name[80] = {0};
  GetClassNameW(control, name, 80);
  if (IsWindowVisible(control) &&
      (!wcscmp(name, L"Edit") || !wcscmp(name, L"Button") ||
       !wcscmp(name, L"Static")))
    describe_control(control);
  return TRUE;
}
static BOOL CALLBACK describe_dialog(HWND candidate, LPARAM context) {
  (void)context;
  DWORD process = 0;
  DWORD thread = GetWindowThreadProcessId(candidate, &process);
  wchar_t name[80] = {0};
  GetClassNameW(candidate, name, 80);
  if (process != GetCurrentProcessId() || !IsWindowVisible(candidate) ||
      wcscmp(name, L"#32770"))
    return TRUE;
  describe_control(candidate);
  GUITHREADINFO info = {.cbSize = sizeof(info)};
  if (GetGUIThreadInfo(thread, &info)) {
    fprintf(stderr, "  Native focused control:\n");
    describe_control(info.hwndFocus);
  }
  EnumChildWindows(candidate, describe_child, 0);
  capture_dialog(candidate);
  return TRUE;
}
static void describe_dialogs(const char *stage) {
  fprintf(stderr, "Native dialog state: %s; app pending=%d; app status=%s\n",
          stage, settings->dialog_pending, settings->status);
  EnumWindows(describe_dialog, 0);
}
typedef struct ControlSearch {
  int id;
  const wchar_t *class_name;
  HWND found;
  unsigned count;
} ControlSearch;
static BOOL CALLBACK find_owned_control(HWND candidate, LPARAM context) {
  ControlSearch *search = (ControlSearch *)context;
  wchar_t name[80] = {0};
  GetClassNameW(candidate, name, 80);
  if (GetDlgCtrlID(candidate) == search->id &&
      !wcscmp(name, search->class_name) && IsWindowVisible(candidate)) {
    search->found = candidate;
    search->count++;
  }
  return TRUE;
}
static HWND owned_control(HWND dialog, int id, const wchar_t *class_name) {
  ControlSearch search = {id, class_name, NULL, 0};
  EnumChildWindows(dialog, find_owned_control, (LPARAM)&search);
  if (search.count != 1 || !IsWindowEnabled(search.found))
    describe_dialogs("Expected owned control is absent, ambiguous or disabled");
  CHECK(search.count == 1 && IsWindowEnabled(search.found) &&
        IsChild(dialog, search.found));
  return search.found;
}
static void choose_file(const char *utf8) {
  if (!settings->dialog_pending)
    fprintf(stderr, "File dialog was not dispatched. Status: %s\n",
            settings->status);
  CHECK(settings->dialog_pending);
  HWND dialog = wait_dialog();
  CHECK(GetForegroundWindow() == dialog ||
        IsChild(dialog, GetForegroundWindow()));
  /* Observed Win32 chooser contracts on this host: Open uses Edit 1148;
   * Save As uses Edit 1001. Both require a unique visible owned control. */
  int filename_id =
      settings->dialog_kind == PP_DIALOG_SAVE_PROJECT ? 1001 : 1148;
  HWND filename = owned_control(dialog, filename_id, L"Edit");
  HWND submit = owned_control(dialog, IDOK, L"Button");
  wchar_t path[1024];
  CHECK(MultiByteToWideChar(CP_UTF8, 0, utf8, -1, path, 1024));
  for (wchar_t *p = path; *p; p++)
    if (*p == L'/')
      *p = L'\\';
  DWORD_PTR accepted = 0;
  CHECK(SendMessageTimeoutW(filename, WM_SETTEXT, 0, (LPARAM)path,
                            SMTO_ABORTIFHUNG, 500, &accepted) &&
        accepted);
  wchar_t readback[1024] = {0};
  DWORD_PTR copied = 0;
  CHECK(SendMessageTimeoutW(filename, WM_GETTEXT, 1024, (LPARAM)readback,
                            SMTO_ABORTIFHUNG, 500, &copied));
  if (wcscmp(readback, path))
    describe_dialogs("Filename readback did not match selected fixture path");
  CHECK(!wcscmp(readback, path));
  describe_dialogs(
      "Owned filename Edit verified before submit-button activation");
  CHECK(SendMessageTimeoutW(submit, BM_CLICK, 0, 0, SMTO_ABORTIFHUNG, 2000,
                            &accepted));
  ULONGLONG until = GetTickCount64() + 10000;
  while (settings->dialog_pending && GetTickCount64() < until)
    pump(30);
  if (settings->dialog_pending) {
    fprintf(stderr, "File chooser did not finish for %s\n", utf8);
    describe_dialogs("Submission timeout");
  }
  CHECK(!settings->dialog_pending);
  CHECK(!IsWindow(dialog));
  pump(80);
}
static void capture(const char *name) {
  char path[MAX_PATH];
  snprintf(path, sizeof(path), "%s/%s.bmp", PP_SMOKE_OUTPUT, name);
  settings->backend->capture_path = path;
  pump(20);
  CHECK(!settings->backend->capture_path);
  printf("Captured %s\n", path);
}
static void create_settings(float scale, bool hidden) {
  CHECK(pp_settings_create_scaled(&settings, app, scale, &error));
  settings->render_hidden = hidden;
  pump(50);
  CHECK(settings->editor.preview_texture);
  if (!hidden) {
    CHECK(SetForegroundWindow(window()));
    pump(30);
    CHECK(GetForegroundWindow() == window());
  }
}
static void hidden_key(PPKey key, unsigned modifiers) {
  PPPlatformEvent event = {.kind = PP_EVENT_KEY_DOWN,
                           .window_id = pp_settings_window_id(settings),
                           .key = key,
                           .modifiers = modifiers};
  pp_settings_event(settings, &event);
  event.kind = PP_EVENT_KEY_UP;
  pp_settings_event(settings, &event);
}
static void hidden_click(const char *id) {
  PPRect bounds;
  CHECK(pp_ui_control_rect(settings->backend, id, &bounds));
  PPPlatformEvent event = {.kind = PP_EVENT_POINTER_MOVE,
                           .window_id = pp_settings_window_id(settings),
                           .button = PP_BUTTON_LEFT,
                           .window_point = {bounds.x + bounds.w / 2,
                                            bounds.y + bounds.h / 2}};
  pp_settings_event(settings, &event);
  event.kind = PP_EVENT_BUTTON_DOWN;
  pp_settings_event(settings, &event);
  event.kind = PP_EVENT_BUTTON_UP;
  pp_settings_event(settings, &event);
}
static void hidden_project_action(bool button, bool save) {
  if (button)
    hidden_click(save ? "save" : "new");
  else
    hidden_key(save ? PP_KEY_S : PP_KEY_N, PP_MOD_CONTROL);
}
static void hidden_text(const char *text) {
  for (const char *p = text; *p; p++) {
    PPPlatformEvent event = {.kind = PP_EVENT_TEXT,
                             .window_id = pp_settings_window_id(settings),
                             .codepoint = (unsigned char)*p};
    pp_settings_event(settings, &event);
  }
}
static void hidden_retained_editor_actions(void) {
  PPAppOptions options = {"", false};
  CHECK(pp_app_create(&app, &options, &error));
  PPAnchor original_anchor = pp_app_ui(app)->anchor;
  uint64_t original_item = pp_app_ui(app)->selected_item;
  CHECK(pp_app_command(app, &(PPCommand){.kind = PP_COMMAND_ADD_TEXT,
                                         .anchor = original_anchor}, &error));
  uint64_t other_item = pp_app_ui(app)->selected_item;
  PPItem other = *pp_project_item(pp_app_project(app), other_item);
  other.text = "Other item";
  CHECK(pp_app_command(app, &(PPCommand){.kind = PP_COMMAND_EDIT_ITEM,
                                         .item_id = other_item, .item = &other},
                       &error));
  create_settings(1, true);
  /* Keep both the selection column and edit field visible for this batch. */
  CHECK(SDL_SetWindowSize(settings->backend->window, 1040, 1800));
  pump(30);
  const char *names[] = {"native close", "Close settings", "anchor selection",
                         "item selection", "Add text", "Remove item"};
  for (int action = 0; action < 6; action++) {
    for (int invalid = 0; invalid < 2; invalid++) {
      fprintf(stderr, "Batched %s + %s\n", invalid ? "invalid size" : "item text",
              names[action]);
      CHECK(pp_app_command(app, &(PPCommand){.kind = PP_COMMAND_SELECT_ANCHOR,
                                             .anchor = original_anchor}, &error));
      CHECK(pp_app_command(app, &(PPCommand){.kind = PP_COMMAND_SELECT_ITEM,
                                             .item_id = original_item}, &error));
      PPItem baseline = *pp_project_item(pp_app_project(app), original_item);
      baseline.text = "Before";
      baseline.text_style.font_size_dip = 64;
      CHECK(pp_app_command(app, &(PPCommand){.kind = PP_COMMAND_EDIT_ITEM,
                                             .item_id = original_item,
                                             .item = &baseline}, &error));
      pp_settings_revert(settings);
      pump(30);
      pp_ui_focus(settings->backend, invalid ? "item-size" : "item-text");
      pump(30);
      hidden_key(PP_KEY_A, PP_MOD_CONTROL);
      pp_settings_draw(settings);
      size_t item_count = pp_project_anchor(pp_app_project(app), original_anchor)->count;
      hidden_text(invalid ? "0" : "After");
      if (action == 0) {
        PPPlatformEvent close = {.kind = PP_EVENT_WINDOW_CLOSE,
                                 .window_id = pp_settings_window_id(settings)};
        pp_settings_event(settings, &close);
      } else if (action == 1)
        hidden_click("close-settings");
      else if (action == 2)
        hidden_click("anchor-1");
      else if (action == 3) {
        char id[48];
        snprintf(id, sizeof(id), "item-%llu", (unsigned long long)other_item);
        hidden_click(id);
      } else
        hidden_click(action == 4 ? "add-text" : "remove-item");
      pp_settings_draw(settings);
      CHECK(!settings->failed && !settings->dialog_pending);
      const PPItem *edited = pp_project_item(pp_app_project(app), original_item);
      CHECK(!strcmp(edited->text, invalid ? "Before" : "After"));
      CHECK(edited->text_style.font_size_dip == 64);
      CHECK(!strcmp(pp_project_item(pp_app_project(app), other_item)->text,
                    "Other item"));
      CHECK(pp_settings_invalid(settings) == (invalid != 0));
      if (action < 2) {
        CHECK(!pp_app_ui(app)->settings_open);
        CHECK(pp_app_command(app, &(PPCommand){.kind = PP_COMMAND_SHOW_SETTINGS},
                             &error));
        pp_settings_draw(settings);
        CHECK(!strcmp(settings->editor.text, invalid ? "Before" : "After"));
        CHECK(!strcmp(settings->editor.size, invalid ? "0" : "64"));
        CHECK(pp_settings_invalid(settings) == (invalid != 0));
      } else {
        CHECK(pp_app_ui(app)->anchor ==
              (action == 2 && !invalid ? PP_ANCHOR_TOP_CENTER : original_anchor));
        if (action == 3 || invalid)
          CHECK(pp_app_ui(app)->selected_item ==
                (action == 3 && !invalid ? other_item : original_item));
      }
      if (action == 4) {
        CHECK(pp_project_anchor(pp_app_project(app), original_anchor)->count ==
              item_count + (invalid ? 0 : 1));
        if (!invalid) {
          CHECK(pp_app_ui(app)->selected_item != original_item);
          CHECK(!strcmp(pp_project_item(pp_app_project(app),
                                        pp_app_ui(app)->selected_item)->text,
                        "After"));
        }
      }
      if (action == 5) {
        CHECK(settings->remove_confirmation == !invalid);
        if (!invalid) {
          hidden_key(PP_KEY_ESCAPE, 0);
          pp_settings_draw(settings);
          CHECK(!settings->remove_confirmation);
        }
      }
      CHECK(!IsWindowVisible(window()) && GetForegroundWindow() != window());
    }
  }
  pp_settings_destroy(settings);
  settings = NULL;
  pp_app_destroy(app);
  app = NULL;
  puts("PASS hidden batched text/invalid draft + close/reopen and selection.");
}
static void hidden_batched_project_actions(void) {
  /* Each case starts with a saved, clean project. Removing the action deferral
   * must fail these checks on saved bytes, draft retention or New protection. */
  snprintf(test_directory, sizeof(test_directory),
           PP_SMOKE_OUTPUT "/settings-batch-%lu-%llu", GetCurrentProcessId(),
           GetTickCount64());
  CHECK(CreateDirectoryA(test_directory, NULL));
  snprintf(package, sizeof(package), "%s/batch.pointy", test_directory);
  for (int button = 0; button < 2; button++) {
    for (int invalid = 0; invalid < 2; invalid++) {
      for (int save = 1; save >= 0; save--) {
        fprintf(stderr, "Batched %s + %s via %s\n",
                invalid ? "invalid size" : "item text", save ? "Save" : "New",
                button ? "button" : "shortcut");
        PPAppOptions options = {"", false};
        CHECK(pp_app_create(&app, &options, &error));
        CHECK(pp_app_command(app, &(PPCommand){.kind = PP_COMMAND_RENAME_PROJECT,
                                               .name = "Baseline"}, &error));
        CHECK(pp_app_command(app, &(PPCommand){.kind = PP_COMMAND_SAVE_AS,
                                               .path = package}, &error));
        create_settings(1, true);
        uint64_t generation = pp_app_generation(app);
        uint64_t item_id = pp_app_ui(app)->selected_item;
        double size = pp_project_item(pp_app_project(app), item_id)
                          ->text_style.font_size_dip;
        pp_ui_focus(settings->backend, invalid ? "item-size" : "item-text");
        pump(30);
        hidden_key(PP_KEY_A, PP_MOD_CONTROL);
        pp_settings_draw(settings);
        if (invalid && save)
          CHECK(pp_app_command(app,
                               &(PPCommand){.kind = PP_COMMAND_RENAME_PROJECT,
                                            .name = "Unsaved model"}, &error));
        const char *text = invalid ? "0" : "Batched item";
        hidden_text(text);
        hidden_project_action(button != 0, save != 0);
        /* Deliberately no draw between text and the project action. */
        pp_settings_draw(settings);
        CHECK(!settings->failed && !settings->dialog_pending);
        CHECK(pp_app_generation(app) == generation);
        if (invalid) {
          CHECK(pp_settings_invalid(settings));
          CHECK(!strcmp(settings->editor.size, "0"));
          CHECK(pp_project_item(pp_app_project(app), item_id)
                    ->text_style.font_size_dip == size);
          CHECK(!pp_app_ui(app)->transition_pending);
          CHECK(pp_app_dirty(app) == (save != 0));
        } else {
          CHECK(!strcmp(pp_project_item(pp_app_project(app), item_id)->text,
                        "Batched item"));
          CHECK(pp_app_dirty(app) == (save == 0));
          CHECK(pp_app_ui(app)->transition_pending == (save == 0));
        }
        PPProject *saved = NULL;
        CHECK(pp_project_load(package, pp_app_assets(app), &saved, &error));
        CHECK(!strcmp(pp_project_name(saved), "Baseline"));
        if (!invalid && save)
          CHECK(!strcmp(pp_project_item(saved, item_id)->text, "Batched item"));
        pp_project_destroy(saved);
        CHECK(!IsWindowVisible(window()) && GetForegroundWindow() != window());
        pp_settings_destroy(settings);
        settings = NULL;
        pp_app_destroy(app);
        app = NULL;
      }
    }
  }
  CHECK(pp_platform_remove_file(package, &error));
  CHECK(RemoveDirectoryA(test_directory));
  package[0] = test_directory[0] = 0;
  puts("PASS hidden batched text/invalid draft + Save/New through shortcuts and buttons.");
}
static void render_only(void) {
  hidden_retained_editor_actions();
  hidden_batched_project_actions();
  INPUT arrow[2];
  key_inputs(VK_DOWN, arrow);
  fprintf(stderr, "Arrow descriptor: scan=%04x flags=%08lx mapped=%08x\n",
          arrow[0].ki.wScan, arrow[0].ki.dwFlags,
          MapVirtualKeyW(VK_DOWN, MAPVK_VK_TO_VSC_EX));
  CHECK((arrow[0].ki.dwFlags & KEYEVENTF_EXTENDEDKEY) &&
        (arrow[1].ki.dwFlags & KEYEVENTF_EXTENDEDKEY));
  CHECK(arrow[0].ki.wScan == 0x50 &&
        (arrow[0].ki.dwFlags & KEYEVENTF_SCANCODE));
  PPAppOptions options = {"", false};
  CHECK(pp_app_create(&app, &options, &error));
  create_settings(1, true);
  capture("task8-hidden-100");
  /* Exercise the real collection popup without opening an OS dialog. */
  CHECK(pp_app_command(
      app,
      &(PPCommand){.kind = PP_COMMAND_IMPORT_FONT,
                   .item_id = pp_app_ui(app)->selected_item,
                   .path = PP_FONT_FIXTURES "/PointyFixture.ttc"},
      &error));
  pump(30);
  pp_ui_focus(settings->backend, "font-face-choice");
  pump(50);
  PPPlatformEvent action = {.kind = PP_EVENT_KEY_DOWN,
                            .window_id = pp_settings_window_id(settings),
                            .key = PP_KEY_ENTER};
  pp_settings_event(settings, &action);
  action.kind = PP_EVENT_KEY_UP;
  pp_settings_event(settings, &action);
  pump(30);
  CHECK(settings->backend->popup_id);
  capture("task8-hidden-font-popup");
  PPRect popup_option;
  CHECK(pp_ui_control_rect(settings->backend, "font-face-choice-option-1",
                           &popup_option));
  CHECK(popup_option.x >= 0 && popup_option.y >= 0 &&
        popup_option.x + popup_option.w <= settings->backend->width &&
        popup_option.y + popup_option.h <= settings->backend->height);
  action.kind = PP_EVENT_KEY_DOWN;
  action.key = PP_KEY_DOWN;
  pp_settings_event(settings, &action);
  action.kind = PP_EVENT_KEY_UP;
  pp_settings_event(settings, &action);
  pump(30);
  action.kind = PP_EVENT_KEY_DOWN;
  action.key = PP_KEY_ENTER;
  pp_settings_event(settings, &action);
  action.kind = PP_EVENT_KEY_UP;
  pp_settings_event(settings, &action);
  pump(30);
  CHECK(!settings->backend->popup_id);
  CHECK(pp_project_item(pp_app_project(app), pp_app_ui(app)->selected_item)
            ->text_style.primary_face == 1);
  CHECK(pp_app_command(app, &(PPCommand){.kind = PP_COMMAND_NEW}, &error));
  CHECK(pp_app_command(app,
                       &(PPCommand){.kind = PP_COMMAND_RESOLVE_TRANSITION,
                                    .choice = PP_UNSAVED_DISCARD},
                       &error));
  CHECK(!IsWindowVisible(window()));
  CHECK(GetForegroundWindow() != window());
  pp_settings_destroy(settings);
  settings = NULL;
  create_settings(1.5f, true);
  capture("task8-hidden-150");
  SDL_SetWindowSize(settings->backend->window, 960, 720);
  pump(50);
  capture("task8-hidden-narrow");
  for (int i = 0; i < 128 && !pp_ui_focused(settings->backend, "easing"); i++) {
    PPPlatformEvent tab = {.kind = PP_EVENT_KEY_DOWN,
                           .window_id = pp_settings_window_id(settings),
                           .key = PP_KEY_TAB};
    pp_settings_event(settings, &tab);
    pump(5);
    tab.kind = PP_EVENT_KEY_UP;
    pp_settings_event(settings, &tab);
    pump(5);
  }
  CHECK(pp_ui_focused(settings->backend, "easing"));
  pump(50);
  PPRect bounds;
  CHECK(pp_ui_control_rect(settings->backend, "easing", &bounds));
  CHECK(bounds.y >= 150 &&
        bounds.y + bounds.h < settings->backend->height - 80);
  capture("task8-hidden-narrow-scrolled");
  CHECK(!IsWindowVisible(window()));
  CHECK(GetForegroundWindow() != window());
  puts("Hidden renderer: native GL, actual emoji preview, 100%/150% controlled "
       "scales and narrow layout passed; no input or activation.");
}
static void desktop_smoke(void) {
  char cwd[MAX_PATH];
  CHECK(GetCurrentDirectoryA(sizeof(cwd), cwd));
  snprintf(test_directory, sizeof(test_directory),
           "%s\\settings-smoke-%lu-%llu", cwd, GetCurrentProcessId(),
           GetTickCount64());
  CHECK(CreateDirectoryA(test_directory, NULL));
  snprintf(package, sizeof(package), "%s\\smoke.pointy", test_directory);
  snprintf(preferences, sizeof(preferences), "%s\\preferences.json",
           test_directory);
  PPAppOptions options = {test_directory, false};
  CHECK(pp_app_create(&app, &options, &error));
  create_settings(1, false);
  printf("Physical window display scale %.2f; controlled UI scale 1.00\n",
         SDL_GetWindowDisplayScale(settings->backend->window));
  replace_field("project-name", L"Smoke presentation");
  CHECK(!strcmp(pp_app_name(app), "Smoke presentation") && pp_app_dirty(app));
  click_control("import-font");
  choose_file(PP_FONT_FIXTURES "/PointyFixture.ttc");
  CHECK(!strcmp(settings->editor.family, "Pointy Fixture"));
  if (dialog_diagnostic) {
    puts("PASS focused font-picker diagnostic: native control dispatch, "
         "filename focus/readback, submission and imported font.");
    return;
  }
  focus_control("font-face-choice");
  key(VK_RETURN);
  pump(40);
  CHECK(settings->backend->popup_id);
  capture("task8-font-popup");
  unsigned previous_down_keys = normalized_down_keys;
  key(VK_DOWN);
  fprintf(stderr,
          "Popup Down: normalized keys=%u->%u, index=%d, keyboard focus=%d\n",
          previous_down_keys, normalized_down_keys,
          settings->backend->popup_index, settings->backend->keyboard_focus);
  CHECK(normalized_down_keys == previous_down_keys + 1 &&
        settings->backend->popup_index == 1);
  key(VK_RETURN);
  pump(50);
  CHECK(!settings->backend->popup_id);
  CHECK(pp_project_item(pp_app_project(app), pp_app_ui(app)->selected_item)
            ->text_style.primary_face == 1);
  replace_field("item-text", L"A");
  CHECK(!strcmp(
      pp_project_item(pp_app_project(app), pp_app_ui(app)->selected_item)->text,
      "A"));
  replace_field("item-size", L"0");
  CHECK(settings->editor.appearance_error[0] &&
        !strcmp(settings->editor.size, "0"));
  chord(VK_CONTROL, 'S');
  CHECK(!settings->dialog_pending && pp_app_dirty(app));
  CHECK(!pp_ui_focused(settings->backend, "save"));
  click_control("revert-fields");
  CHECK(!pp_settings_invalid(settings));
  replace_field("item-size", L"120");
  CHECK(pp_project_item(pp_app_project(app), pp_app_ui(app)->selected_item)
            ->text_style.font_size_dip == 120);
  /* Overlay exactly covers a settings control; native topmost order and the
   * selective hook must route the original click to settings. */
  focus_control("anchor-enabled");
  POINT point = control_center("anchor-enabled");
  uint8_t pixels[40 * 40 * 4], alpha[40 * 40];
  memset(pixels, 255, sizeof(pixels));
  memset(alpha, 255, sizeof(alpha));
  PPFrame frame = {pixels, alpha, 40, 40, 160};
  CHECK(pp_surface_create(&overlap, &error));
  CHECK(pp_surface_present(
      overlap, &frame, (PPRect){point.x - 20, point.y - 20, 40, 40}, &error));
  CHECK(GetAncestor(WindowFromPoint(point), GA_ROOT) == window());
  click_control("anchor-enabled");
  CHECK(!pp_project_anchor(pp_app_project(app), PP_ANCHOR_TOP_LEFT)->enabled &&
        !sprite_presses);
  pp_surface_destroy(overlap);
  overlap = NULL;
  click_control("anchor-enabled");
  click_control("add-image");
  choose_file(PP_MEDIA_FIXTURES "/alpha.png");
  CHECK(pp_project_anchor(pp_app_project(app), PP_ANCHOR_TOP_LEFT)->count == 2);
  click_control("save-as");
  choose_file(package);
  CHECK(!pp_app_dirty(app) && !strcmp(pp_app_path(app), package));
  uint64_t generation = pp_app_generation(app);
  click_control("open");
  choose_file(package);
  CHECK(pp_app_generation(app) == generation + 1 && !pp_app_dirty(app));
  capture("task8-settings-100");
  replace_field("project-name", L"Unsaved presentation");
  click_control("new");
  CHECK(pp_app_ui(app)->transition_pending);
  CHECK(pp_ui_focused(settings->backend, "cancel-transition"));
  key(VK_ESCAPE);
  CHECK(!pp_app_ui(app)->transition_pending && pp_app_dirty(app));
  CHECK(pp_ui_focused(settings->backend, "new"));
  SDL_SetWindowSize(settings->backend->window, 700, 600);
  pump(50);
  capture("task8-settings-narrow");
  focus_control("easing");
  CHECK(pp_ui_focused(settings->backend, "easing"));
  capture("task8-settings-narrow-scrolled");
  click_control("close-settings");
  CHECK(!pp_app_ui(app)->settings_open && pp_app_dirty(app) &&
        !pp_app_ui(app)->quit_requested && !IsWindowVisible(window()));
  CHECK(pp_app_command(app, &(PPCommand){.kind = PP_COMMAND_SHOW_SETTINGS},
                       &error));
  pump(40);
  pp_settings_destroy(settings);
  settings = NULL;
  CHECK(pp_app_command(app,
                       &(PPCommand){.kind = PP_COMMAND_SET_GEOMETRY,
                                    .geometry = {100, 100, 1040, 780}},
                       &error));
  create_settings(1.5f, false);
  capture("task8-settings-150");
  printf("PASS actual control editing, font collection popup, invalid draft "
         "guard, image import, native Save As/Open, modal Escape/focus, sprite "
         "overlap, resize and dirty settings close.\n");
}
static void manual_font_smoke(void) {
  PPAppOptions options = {"", false};
  CHECK(pp_app_create(&app, &options, &error));
  CHECK(pp_app_command(app,
                       &(PPCommand){.kind = PP_COMMAND_RENAME_PROJECT,
                                    .name = "Smoke presentation"},
                       &error));
  create_settings(1, false);
  pp_settings_dialog(settings, PP_DIALOG_IMPORT_FONT, false);
  HWND dialog = wait_dialog();
  CHECK(GetForegroundWindow() == dialog ||
        IsChild(dialog, GetForegroundWindow()));
  describe_dialogs("Manual font picker ready; no synthetic input");
  puts("READY: choose "
       "D:\\Dev\\PointyPointer\\tests\\fixtures\\fonts\\PointyFixture.ttc in "
       "the open font picker. Manual wait is bounded to 180 seconds.");
  fflush(stdout);
  fflush(stderr);
  ULONGLONG until = GetTickCount64() + 180000;
  while (settings->dialog_pending && GetTickCount64() < until)
    pump(30);
  if (settings->dialog_pending)
    describe_dialogs("Manual selection timeout");
  CHECK(!settings->dialog_pending);
  pump(50);
  const PPItem *item =
      pp_project_item(pp_app_project(app), pp_app_ui(app)->selected_item);
  PPAsset *font =
      pp_asset_retain(pp_app_assets(app), item->text_style.primary_asset);
  CHECK(font);
  PPBytes source = pp_asset_source(font);
  PPFontInfo info;
  CHECK(pp_font_inspect(source, item->text_style.primary_face, &info, &error));
  fprintf(stderr,
          "Manual selected result: family=%s faces=%zu face=%d bytes=%zu "
          "status=%s\n",
          info.family, info.face_count, item->text_style.primary_face,
          source.size, settings->status);
  FILE *fixture = pp_platform_open_file(PP_FONT_FIXTURES "/PointyFixture.ttc",
                                        "rb", &error);
  CHECK(fixture);
  uint64_t size = 0;
  CHECK(pp_platform_file_size(fixture, &size) && size == source.size);
  uint8_t *expected = malloc((size_t)size);
  CHECK(expected);
  CHECK(fread(expected, 1, (size_t)size, fixture) == size);
  fclose(fixture);
  CHECK(!memcmp(expected, source.data, (size_t)size));
  free(expected);
  CHECK(!strcmp(info.family, "Pointy Fixture") && info.face_count == 2);
  pp_asset_release(font);
  capture("task8-manual-font-import");
  puts("PASS manual native font-picker selection; imported bytes exactly match "
       "PointyFixture.ttc.");
}
int main(int argc, char **argv) {
  manual_font = argc == 2 && !strcmp(argv[1], "--manual-font");
  dialog_diagnostic = argc == 2 && !strcmp(argv[1], "--dialog-diagnostic");
  desktop_run =
      dialog_diagnostic || (argc == 2 && !strcmp(argv[1], "--desktop"));
  CHECK(argc == 2 &&
        (desktop_run || manual_font || !strcmp(argv[1], "--render-only")));
  CHECK(GetPhysicalCursorPos(&original_cursor));
  original_foreground = GetForegroundWindow();
  atexit(cleanup);
  if (manual_font)
    manual_font_smoke();
  else if (desktop_run)
    desktop_smoke();
  else
    render_only();
  return 0;
}
