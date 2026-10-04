#include "pointy/platform.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef struct PPDialogRequest {
  PPDialogCallback callback;
  void *userdata;
  char *path;
  PPError error;
  bool ready;
  struct PPDialogRequest *next;
} PPDialogRequest;
static SRWLOCK dialog_lock = SRWLOCK_INIT;
static PPDialogRequest *requests;
static uint32_t parent_id;
static HWND settings_window;

HWND pp_windows_settings_window(void) {
  return settings_window && IsWindowVisible(settings_window) ? settings_window
                                                             : NULL;
}
void pp_platform_settings_window(uint32_t window_id) {
  SDL_Window *window = SDL_GetWindowFromID(window_id);
  settings_window =
      window ? SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                      SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL)
             : NULL;
}
void pp_platform_dialog_parent(uint32_t window_id) { parent_id = window_id; }
static void dialog_result(void *userdata, const char *const *files,
                          int filter) {
  (void)filter;
  PPDialogRequest *request = userdata;
  /* SDL owns files and SDL_GetError only through this callback. Capture both
   * before returning, even when the platform invokes us on another thread. */
  AcquireSRWLockExclusive(&dialog_lock);
  if (!files) {
    request->error.code = PP_ERROR_PLATFORM;
    snprintf(request->error.message, sizeof(request->error.message),
             "File dialog: %s", SDL_GetError());
  } else if (files[0]) {
    size_t size = strlen(files[0]) + 1;
    request->path = malloc(size);
    if (request->path)
      memcpy(request->path, files[0], size);
    else {
      request->error.code = PP_ERROR_MEMORY;
      snprintf(request->error.message, sizeof(request->error.message),
               "Cannot copy the chosen file path");
    }
  }
  request->ready = true;
  ReleaseSRWLockExclusive(&dialog_lock);
}
void pp_platform_file_dialog(PPDialogKind kind, const char *suggested_path,
                             PPDialogCallback callback, void *userdata) {
  static const SDL_DialogFileFilter project[] = {
      {"PointyPointer project", "pointy"}};
  static const SDL_DialogFileFilter images[] = {
      {"Images (PNG, GIF, static SVG)", "png;gif;svg"}};
  static const SDL_DialogFileFilter fonts[] = {
      {"Fonts (TTF, OTF and collections)", "ttf;otf;ttc;otc"}};
  PPDialogRequest *request = calloc(1, sizeof(*request));
  if (!request) {
    PPError error = {PP_ERROR_MEMORY, "Cannot allocate file dialog"};
    if (callback)
      callback(userdata, NULL, &error);
    return;
  }
  request->callback = callback;
  request->userdata = userdata;
  AcquireSRWLockExclusive(&dialog_lock);
  request->next = requests;
  requests = request;
  ReleaseSRWLockExclusive(&dialog_lock);
  SDL_Window *parent = SDL_GetWindowFromID(parent_id);
  const SDL_DialogFileFilter *filters = kind == PP_DIALOG_IMPORT_IMAGE ? images
                                        : kind == PP_DIALOG_IMPORT_FONT
                                            ? fonts
                                            : project;
  if (kind == PP_DIALOG_SAVE_PROJECT)
    SDL_ShowSaveFileDialog(dialog_result, request, parent, filters, 1,
                           suggested_path);
  else
    SDL_ShowOpenFileDialog(dialog_result, request, parent, filters, 1,
                           suggested_path, false);
}
void pp_platform_cancel_file_dialogs(void *userdata) {
  AcquireSRWLockExclusive(&dialog_lock);
  for (PPDialogRequest *r = requests; r; r = r->next)
    if (r->userdata == userdata)
      r->callback = NULL;
  ReleaseSRWLockExclusive(&dialog_lock);
}
void pp_platform_poll_dialogs(void) {
  for (;;) {
    AcquireSRWLockExclusive(&dialog_lock);
    PPDialogRequest **link = &requests;
    while (*link && !(*link)->ready)
      link = &(*link)->next;
    PPDialogRequest *request = *link;
    if (request)
      *link = request->next;
    ReleaseSRWLockExclusive(&dialog_lock);
    if (!request)
      break;
    if (request->callback)
      request->callback(request->userdata, request->path,
                        request->error.code ? &request->error : NULL);
    free(request->path);
    free(request);
  }
}
