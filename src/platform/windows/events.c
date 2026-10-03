#include "windows_internal.h"
#include <windowsx.h>
#include <stdlib.h>
typedef struct EventNode { PPPlatformEvent event; struct EventNode *next; } EventNode;
static EventNode *first, *last;
void pp_windows_record(PPSurface *surface, HWND window, UINT message, WPARAM w, LPARAM l) {
    PPPlatformEvent event = {0}; event.surface = surface;
    switch (message) {
    case WM_MOUSEMOVE: event.kind = PP_EVENT_POINTER_MOVE; break;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: event.kind = PP_EVENT_BUTTON_DOWN; break;
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: event.kind = PP_EVENT_BUTTON_UP; break;
    case WM_MOUSEWHEEL: event.kind = PP_EVENT_WHEEL; event.wheel_delta = GET_WHEEL_DELTA_WPARAM(w)/(double)WHEEL_DELTA; break;
    default: return;
    }
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) event.button = PP_BUTTON_LEFT;
    if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP) event.button = PP_BUTTON_RIGHT;
    if (message == WM_MBUTTONDOWN || message == WM_MBUTTONUP) event.button = PP_BUTTON_MIDDLE;
    POINT point = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    if (message != WM_MOUSEWHEEL) ClientToScreen(window, &point);
    event.desktop_point = (PPVec2){point.x, point.y};
    if (GetKeyState(VK_SHIFT) < 0) event.modifiers |= PP_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) < 0) event.modifiers |= PP_MOD_CONTROL;
    if (GetKeyState(VK_MENU) < 0) event.modifiers |= PP_MOD_ALT;
    LARGE_INTEGER counter, frequency; QueryPerformanceCounter(&counter); QueryPerformanceFrequency(&frequency);
    event.timestamp = (double)counter.QuadPart / (double)frequency.QuadPart;
    if (event.kind == PP_EVENT_POINTER_MOVE && last && last->event.kind == PP_EVENT_POINTER_MOVE && last->event.surface == surface) {
        last->event = event; return;
    }
    EventNode *node = malloc(sizeof(*node));
    if (!node) return;
    node->event = event; node->next = NULL;
    if (last) last->next = node; else first = node;
    last = node;
}
void pp_windows_forget(PPSurface *surface) {
    EventNode **link = &first; last = NULL;
    while (*link) {
        EventNode *node = *link;
        if (node->event.surface == surface) { *link = node->next; free(node); }
        else { last = node; link = &node->next; }
    }
}
bool pp_platform_poll(PPPlatformEvent *out) {
    if (!out) return false;
    MSG message;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) { *out = (PPPlatformEvent){0}; out->kind = PP_EVENT_QUIT; return true; }
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    if (!first) return false;
    EventNode *node = first; *out = node->event; first = node->next;
    if (!first) last = NULL;
    free(node); return true;
}
