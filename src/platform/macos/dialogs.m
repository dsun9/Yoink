#include "pointy/platform.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#import "macos_internal.h"
#include <pthread.h>

typedef struct PPDialogRequest {
  PPDialogCallback callback;
  void *userdata;
  char *path;
  PPError error;
  bool ready;
  struct PPDialogRequest *next;
} PPDialogRequest;
static pthread_mutex_t dialog_lock=PTHREAD_MUTEX_INITIALIZER;
static PPDialogRequest *requests;
static uint32_t parent_id;
static NSWindow *settings_window,*drawer_window;
static NSRunningApplication *previous_app;
static NSWindow *previous_key;
NSWindow *pp_mac_window(uint32_t id) {
    SDL_Window *window=SDL_GetWindowFromID(id);
    return window?SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,NULL):nil;
}
NSWindow *pp_mac_drawer(void) {return drawer_window.visible?drawer_window:nil;}
void pp_mac_prioritize(void) {
    /* A separate level protects both UI windows without reordering sprites or
     * activating the application during every animation frame. */
    if(settings_window)settings_window.level=NSFloatingWindowLevel+1;
    if(drawer_window)drawer_window.level=NSFloatingWindowLevel+2;
}
void pp_platform_settings_window(uint32_t id) {settings_window=pp_mac_window(id);pp_mac_prioritize();}
void pp_platform_drawer_window(uint32_t id) {
    [previous_app release];[previous_key release];
    previous_app=[NSWorkspace.sharedWorkspace.frontmostApplication retain];
    previous_key=[NSApp.keyWindow retain];
    drawer_window=pp_mac_window(id);pp_mac_prioritize();
}
void pp_platform_drawer_close(bool restore) {
    bool owned=drawer_window&&NSApp.active&&NSApp.keyWindow==drawer_window;
    [drawer_window orderOut:nil];drawer_window=nil;
    if(restore&&owned) {
        if(previous_app.processIdentifier==NSProcessInfo.processInfo.processIdentifier) {
            if(previous_key.visible)[previous_key makeKeyWindow];
        } else if(previous_app&&!previous_app.terminated) [previous_app activateWithOptions:0];
    }
    [previous_app release];previous_app=nil;[previous_key release];previous_key=nil;
}
void pp_platform_dialog_parent(uint32_t id) {parent_id=id;}
static void dialog_result(void *userdata, const char *const *files,
                          int filter) {
  (void)filter;
  PPDialogRequest *request = userdata;
  /* SDL owns files and SDL_GetError only through this callback. Capture both
   * before returning, even when the platform invokes us on another thread. */
  pthread_mutex_lock(&dialog_lock);
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
  pthread_mutex_unlock(&dialog_lock);
}
void pp_platform_file_dialog(PPDialogKind kind, const char *suggested_path,
                             PPDialogCallback callback, void *userdata) {
  static const SDL_DialogFileFilter project[] = {
      {"Yoink project", "pointy"}};
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
  pthread_mutex_lock(&dialog_lock);
  request->next = requests;
  requests = request;
  pthread_mutex_unlock(&dialog_lock);
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
  pthread_mutex_lock(&dialog_lock);
  for (PPDialogRequest *r = requests; r; r = r->next)
    if (r->userdata == userdata)
      r->callback = NULL;
  pthread_mutex_unlock(&dialog_lock);
}
void pp_platform_poll_dialogs(void) {
  for (;;) {
    pthread_mutex_lock(&dialog_lock);
    PPDialogRequest **link = &requests;
    while (*link && !(*link)->ready)
      link = &(*link)->next;
    PPDialogRequest *request = *link;
    if (request)
      *link = request->next;
    pthread_mutex_unlock(&dialog_lock);
    if (!request)
      break;
    if (request->callback)
      request->callback(request->userdata, request->path,
                        request->error.code ? &request->error : NULL);
    free(request->path);
    free(request);
  }
}
