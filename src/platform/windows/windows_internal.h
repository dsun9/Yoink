#ifndef POINTY_WINDOWS_INTERNAL_H
#define POINTY_WINDOWS_INTERNAL_H
#include "pointy/platform.h"
#include <windows.h>
typedef struct SDL_Window SDL_Window;
struct PPSurface {
    HWND window; HDC dc; HBITMAP bitmap; HGDIOBJ original;
    uint8_t *bits; int width,height;
    POINT origin;
    uint64_t instance_id;
    struct PPSurface *input_next;
    SDL_Window *gl_window;
    WNDPROC gl_procedure;
    bool gl_video_initialized, gl_interactive;
    bool rectangular;
};
void pp_windows_record(PPSurface *surface, HWND window, UINT message, WPARAM w, LPARAM l);
void pp_windows_forget(PPSurface *surface);
bool pp_windows_enqueue(PPPlatformEvent event);
PPPlatformEvent pp_windows_event(PPSurface *surface, UINT message, POINT point, DWORD wheel_data);
bool pp_windows_input_add(PPSurface *surface, PPError *error);
void pp_windows_input_remove(PPSurface *surface);
void pp_windows_input_cancel(PPSurface *surface);
bool pp_windows_input_owned(PPSurface *surface);
PPSurface *pp_windows_input_owner(void);
void pp_windows_input_check(void);
HWND pp_windows_settings_window(void);
void pp_windows_gl_destroy(PPSurface *surface);
#endif
