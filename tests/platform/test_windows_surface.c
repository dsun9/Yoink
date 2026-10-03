#include "pointy/platform.h"
#include "receiver_shared.h"
#include "../test.h"
#include <math.h>
#include <string.h>
static PROCESS_INFORMATION child;
static ReceiverState *receiver;
static HANDLE mapping;
static PPSurface *surface;
static PPRenderer *renderer;
static HWND old_foreground;
static POINT old_cursor;
static void cleanup(void) {
    pp_surface_destroy(surface); pp_renderer_destroy(renderer);
    if (receiver && receiver->window) PostMessageW((HWND)receiver->window, WM_CLOSE, 0, 0);
    if (child.hProcess) { WaitForSingleObject(child.hProcess, 3000); CloseHandle(child.hProcess); CloseHandle(child.hThread); }
    if (receiver) UnmapViewOfFile(receiver);
    if (mapping) CloseHandle(mapping);
    SetCursorPos(old_cursor.x, old_cursor.y);
    if (old_foreground) SetForegroundWindow(old_foreground);
}
static int overlay_clicks, overlay_right, overlay_wheels;
static void pump(void) {
    ULONGLONG until = GetTickCount64() + 100;
    do {
        PPPlatformEvent event;
        while (pp_platform_poll(&event)) {
            if (event.kind == PP_EVENT_BUTTON_DOWN && event.surface == surface) {
                if (event.button == PP_BUTTON_LEFT) overlay_clicks++;
                if (event.button == PP_BUTTON_RIGHT) overlay_right++;
            }
            if (event.kind == PP_EVENT_WHEEL) overlay_wheels++;
        }
        Sleep(1);
    } while (GetTickCount64() < until);
}
static void press(bool right) {
    INPUT input[2] = {0}; input[0].type = input[1].type = INPUT_MOUSE;
    input[0].mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
    input[1].mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
    CHECK(SendInput(2, input, sizeof(INPUT)) == 2);
    pump();
}
static void click(int x, int y, bool right) {
    /* Arrival and click are deliberately adjacent, with no pump or hover delay. */
    CHECK(SetCursorPos(x, y)); press(right);
}
static PPFrame ring(void) {
    PPFrame frame = {0}; frame.width = frame.height = 64; frame.stride = 256;
    frame.pixels = calloc(64, 256); frame.content_alpha = calloc(64, 64);
    CHECK(frame.pixels && frame.content_alpha);
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        double radius = hypot(x - 31.5, y - 31.5);
        if (radius >= 18 && radius <= 29) {
            size_t i = (size_t)y * 64 + x;
            frame.pixels[i*4] = 255; frame.pixels[i*4+3] = 255;
            frame.content_alpha[i] = 255;
        }
    }
    return frame;
}
static void test_windows_surface_routes_alpha(void) {
    PPError error = {0}; CHECK(pp_surface_create(&surface, &error));
    PPFrame source = ring(); PPRect bounds = {220, 220, 64, 64};
    CHECK(pp_surface_present(surface, &source, bounds, &error)); pump();
    HWND foreground_before = GetForegroundWindow();
    click(252, 252, false); CHECK(receiver->clicks == 1); CHECK(overlay_clicks == 0);
    click(277, 252, false); CHECK(receiver->clicks == 1); CHECK(overlay_clicks == 1);
    CHECK(GetForegroundWindow() == foreground_before);
    click(252, 252, true); CHECK(receiver->right_clicks == 1); CHECK(overlay_right == 0);
    click(277, 252, true); CHECK(receiver->right_clicks == 1); CHECK(overlay_right == 1);
    CHECK(GetForegroundWindow() == foreground_before);
    /* Keep the pointer fixed after rim arrival, remove that pixel, click. */
    memset(source.pixels, 0, 64*256); memset(source.content_alpha, 0, 64*64);
    CHECK(pp_surface_present(surface, &source, bounds, &error));
    press(false); CHECK(receiver->clicks == 2); CHECK(overlay_clicks == 1);
    CHECK(GetForegroundWindow() == foreground_before);
    pp_frame_destroy(&source); source = ring();
    CHECK(pp_renderer_create(&renderer, &error)); PPFrame rotated = {0};
    CHECK(pp_renderer_transform(renderer, &source, 64, 37, 1, 1, &rotated, &error));
    CHECK(rotated.width == 90 && rotated.height == 90);
    bounds = (PPRect){220,220,rotated.width,rotated.height};
    CHECK(pp_surface_present(surface, &rotated, bounds, &error));
    click(220 + rotated.width/2, 220 + rotated.height/2, false);
    CHECK(receiver->clicks == 3); CHECK(overlay_clicks == 1);
    CHECK(GetForegroundWindow() == foreground_before);
    CHECK(rotated.content_alpha[(size_t)(rotated.height/2)*rotated.width+rotated.width/2] == 0);
    INPUT wheel = {0}; wheel.type = INPUT_MOUSE; wheel.mi.dwFlags = MOUSEEVENTF_WHEEL; wheel.mi.mouseData = WHEEL_DELTA;
    CHECK(SendInput(1, &wheel, sizeof(wheel)) == 1); pump();
    printf("PASS alpha routing: receiver left=3 right=1; overlay left=1 right=1; focus preserved; rotated hole passes.\n");
    printf("Wheel observation at rotated hole: receiver=%ld overlay=%d (Task 2 gate pending).\n", receiver->wheels, overlay_wheels);
    pp_frame_destroy(&source); pp_frame_destroy(&rotated);
}
static void test_renderer_preserves_rows_and_premultiplication(void) {
    PPFrame source = {0}; source.width = 2; source.height = 2; source.stride = 8;
    const uint8_t rgba[] = {128,0,0,128, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    source.pixels = malloc(sizeof(rgba));
    source.content_alpha = malloc(4); CHECK(source.pixels && source.content_alpha);
    memcpy(source.pixels, rgba, sizeof(rgba));
    memcpy(source.content_alpha, (uint8_t[]){128,255,255,255}, 4);
    PPError error = {0}; PPFrame out = {0};
    CHECK(pp_renderer_transform(renderer, &source, 2, 0, 0.5, 1, &out, &error));
    CHECK(out.width == 2 && out.height == 2);
    CHECK(out.pixels[0] == 64 && out.pixels[3] == 64);
    /* 255*0.5 falls halfway between UNORM bytes; allow either adjacent byte,
     * while requiring equal premultiplied color and alpha for these primaries. */
    CHECK(out.pixels[5] >= 127 && out.pixels[5] <= 128 && out.pixels[7] == out.pixels[5]);
    CHECK(out.pixels[10] >= 127 && out.pixels[10] <= 128 && out.pixels[11] == out.pixels[10]);
    CHECK(out.content_alpha[0] == 128 && out.content_alpha[3] == 255);
    pp_frame_destroy(&out);
    CHECK(!pp_renderer_transform(renderer, &source, 0, 0, 1, 1, &out, &error));
    CHECK(error.code == PP_ERROR_ARGUMENT && !out.pixels);
    pp_frame_destroy(&source);
    puts("PASS GL transform: top-down RGBA, premultiplied opacity, content alpha, invalid dimensions.");
}
static void test_failed_presentation_removes_interceptor(void) {
    PPError error = {0};
    CHECK(!pp_surface_present(surface, NULL, (PPRect){0}, &error));
    CHECK(error.code == PP_ERROR_ARGUMENT);
    click(290,265,false); CHECK(receiver->clicks == 4 && overlay_clicks == 1);
    puts("PASS failed presentation: native interceptor removed, next click reaches receiver.");
}
int main(int argc, char **argv) {
    CHECK(argc == 2); old_foreground = GetForegroundWindow(); GetCursorPos(&old_cursor); atexit(cleanup);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    char name[80]; snprintf(name, sizeof(name), "Local\\PointyReceiver-%lu", GetCurrentProcessId());
    mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(ReceiverState), name); CHECK(mapping);
    receiver = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*receiver)); CHECK(receiver);
    char command[2048]; snprintf(command, sizeof(command), "\"%s\" %s", argv[1], name);
    STARTUPINFOA startup = {sizeof(startup)};
    CHECK(CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &child));
    ULONGLONG until = GetTickCount64() + 5000;
    while (!receiver->window && GetTickCount64() < until) Sleep(10);
    CHECK(receiver->window); CHECK(SetForegroundWindow((HWND)receiver->window));
    test_windows_surface_routes_alpha(); test_renderer_preserves_rows_and_premultiplication();
    test_failed_presentation_removes_interceptor(); return 0;
}
