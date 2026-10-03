#ifndef POINTY_WINDOWS_INTERNAL_H
#define POINTY_WINDOWS_INTERNAL_H
#include "pointy/platform.h"
#include <windows.h>
void pp_windows_record(PPSurface *surface, HWND window, UINT message, WPARAM w, LPARAM l);
void pp_windows_forget(PPSurface *surface);
#endif
