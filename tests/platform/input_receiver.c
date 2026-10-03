#include "receiver_shared.h"
#include <stdio.h>
static ReceiverState *state;
static LRESULT CALLBACK receive(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_LBUTTONDOWN) InterlockedIncrement(&state->clicks);
    if (message == WM_RBUTTONDOWN) InterlockedIncrement(&state->right_clicks);
    if (message == WM_MOUSEWHEEL) InterlockedIncrement(&state->wheels);
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, w, l);
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, argv[1]);
    if (!mapping) return 3;
    state = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*state));
    if (!state) { CloseHandle(mapping); return 4; }
    WNDCLASSW type = {0};
    type.hInstance = GetModuleHandleW(NULL); type.lpfnWndProc = receive;
    type.lpszClassName = L"PointyPointer.InputReceiver";
    type.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&type);
    HWND window = CreateWindowExW(0, type.lpszClassName, L"PointyPointer routing receiver",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 160, 160, 360, 320, NULL, NULL, type.hInstance, NULL);
    state->window = (ULONG_PTR)window;
    MSG message;
    while (window && GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    UnmapViewOfFile(state); CloseHandle(mapping);
    return window ? 0 : 5;
}
