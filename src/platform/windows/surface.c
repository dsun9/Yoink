/* Layered DIB presentation adapted from BongoCat's windows_layered.c,
 * copyright its contributors, AGPL-3.0-or-later (see repository LICENSE).
 * The native window directly owns input; no reference proxy/source routing. */
#include "windows_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static const wchar_t surface_class[] = L"PointyPointer.Surface";
static void release_bitmap(PPSurface *surface);
static bool fail(PPSurface *surface, PPError *error, int code, const char *message) {
    DWORD failure = GetLastError();
    if (surface && surface->window) { DestroyWindow(surface->window); surface->window = NULL; pp_windows_forget(surface); }
    /* No surviving DIB may outlive a failed presentation reservation. */
    if (surface) release_bitmap(surface);
    if (error) {
        error->code = code;
        if (code == PP_ERROR_PLATFORM) snprintf(error->message, sizeof(error->message), "%s (Win32 %lu)", message, failure);
        else snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return false;
}
static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCCREATE) SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)l)->lpCreateParams);
    PPSurface *surface = (PPSurface *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (surface) pp_windows_record(surface, window, message, w, l);
    return DefWindowProcW(window, message, w, l);
}
bool pp_surface_create(PPSurface **out, PPError *error) {
    if (!out) return fail(NULL, error, PP_ERROR_ARGUMENT, "Missing surface output");
    *out = NULL;
    WNDCLASSEXW existing = {sizeof(existing)};
    HINSTANCE instance = GetModuleHandleW(NULL);
    if (!GetClassInfoExW(instance, surface_class, &existing)) {
        WNDCLASSEXW type = {0}; type.cbSize = sizeof(type); type.lpfnWndProc = procedure;
        type.hInstance = instance; type.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512)); type.lpszClassName = surface_class;
        if (!RegisterClassExW(&type)) return fail(NULL, error, PP_ERROR_PLATFORM, "Cannot register overlay class");
    }
    PPSurface *surface = calloc(1, sizeof(*surface));
    if (!surface) return fail(NULL, error, PP_ERROR_MEMORY, "Cannot allocate surface");
    surface->dc = CreateCompatibleDC(NULL);
    /* Declare topmost at creation: background promotion with SetWindowPos can
     * be denied after the user has clicked another process's window. */
    surface->window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        surface_class, L"PointyPointer overlay", WS_POPUP, 0, 0, 1, 1, NULL, NULL, instance, surface);
    if (!surface->dc || !surface->window) {
        fail(surface, error, PP_ERROR_PLATFORM, "Cannot create overlay"); pp_surface_destroy(surface); return false;
    }
    if (!pp_windows_input_add(surface, error)) { pp_surface_destroy(surface); return false; }
    *out = surface; return true;
}
static void release_bitmap(PPSurface *surface) {
    if (surface->bitmap) { SelectObject(surface->dc, surface->original); DeleteObject(surface->bitmap); }
    surface->bitmap = NULL; surface->bits = NULL;
}
bool pp_surface_present(PPSurface *surface, const PPFrame *frame, PPRect bounds, PPError *error) {
    if (!surface || !surface->window || !frame || !frame->pixels || !frame->content_alpha || frame->width <= 0 || frame->height <= 0 ||
        frame->width > 16384 || frame->height > 16384 || frame->stride < (size_t)frame->width*4 ||
        !isfinite(bounds.x) || !isfinite(bounds.y) || bounds.x < -2147483647.0 || bounds.x > 2147483647.0 ||
        bounds.y < -2147483647.0 || bounds.y > 2147483647.0 || bounds.w != frame->width || bounds.h != frame->height)
        return fail(surface, error, PP_ERROR_ARGUMENT, "Invalid overlay frame or bounds");
    if (!surface->bitmap || surface->width != frame->width || surface->height != frame->height) {
        release_bitmap(surface);
        BITMAPINFO info = {0}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = frame->width; info.bmiHeader.biHeight = -frame->height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        surface->bitmap = CreateDIBSection(surface->dc, &info, DIB_RGB_COLORS, (void **)&surface->bits, NULL, 0);
        if (!surface->bitmap) return fail(surface, error, PP_ERROR_PLATFORM, "Cannot create overlay bitmap");
        surface->original = SelectObject(surface->dc, surface->bitmap);
        surface->width = frame->width; surface->height = frame->height;
    }
    for (int y = 0; y < frame->height; y++) for (int x = 0; x < frame->width; x++) {
        const uint8_t *rgba = frame->pixels + (size_t)y*frame->stride + x*4;
        uint8_t *bgra = surface->bits + ((size_t)y*frame->width+x)*4;
        bgra[0] = rgba[2]; bgra[1] = rgba[1]; bgra[2] = rgba[0]; bgra[3] = rgba[3];
    }
    POINT destination = {(LONG)llround(bounds.x), (LONG)llround(bounds.y)}, source = {0,0};
    SIZE size = {frame->width, frame->height}; BLENDFUNCTION blend = {AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    HDC screen = GetDC(NULL);
    BOOL ok = screen && UpdateLayeredWindow(surface->window, screen, &destination, &size, surface->dc, &source, 0, &blend, ULW_ALPHA);
    DWORD failure = GetLastError(); if (screen) ReleaseDC(NULL, screen);
    if (!ok) { SetLastError(failure); return fail(surface,error,PP_ERROR_PLATFORM,"Cannot present overlay"); }
    surface->origin = destination;
    HWND settings = pp_windows_settings_window();
    UINT order=IsWindowVisible(surface->window)?SWP_NOZORDER:0;
    if (!SetWindowPos(surface->window, settings ? settings : HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW|order))
        return fail(surface,error,PP_ERROR_PLATFORM,"Cannot place overlay");
    return true;
}
void pp_surface_destroy(PPSurface *surface) {
    if (!surface) return;
    pp_windows_input_remove(surface);
    if (surface->window) DestroyWindow(surface->window);
    pp_windows_forget(surface); release_bitmap(surface);
    if (surface->dc) DeleteDC(surface->dc);
    free(surface);
}
void pp_surface_hide(PPSurface *surface) {
    if(!surface) return;
    pp_windows_input_cancel(surface);
    if(surface->window) ShowWindow(surface->window,SW_HIDE);
}
