#include "windows_internal.h"
#include <windowsx.h>
/* Fixed queue: hooks never allocate. Consecutive moves are coalesced. */
#define EVENT_CAPACITY 512
static PPPlatformEvent events[EVENT_CAPACITY];
static unsigned first,count;
static void discard_moves(PPSurface *keep_owner) {
    unsigned kept=0;
    for(unsigned i=0;i<count;i++) {
        PPPlatformEvent event=events[(first+i)%EVENT_CAPACITY];
        if(event.kind!=PP_EVENT_POINTER_MOVE || (keep_owner && event.surface==keep_owner))
            events[(first+kept++)%EVENT_CAPACITY]=event;
    }
    count=kept;
}
static void append_cancellation(PPPlatformEvent event) {
    for(unsigned i=0;i<count;i++) {
        PPPlatformEvent *queued=&events[(first+i)%EVENT_CAPACITY];
        if(queued->surface==event.surface && queued->instance_id==event.instance_id) return;
    }
    event.kind=PP_EVENT_CANCEL;event.button=PP_BUTTON_NONE;
    events[(first+count++)%EVENT_CAPACITY]=event;
}
static void cancel_discarded_gestures(PPPlatformEvent incoming,PPSurface *owner) {
    unsigned discarded=count;count=0;
    /* Compact in place: output never advances past the input cursor. Preserve
     * every affected left-button gesture, including releases queued after
     * native ownership ended. Deduplicate cancellations by surface and ID. */
    for(unsigned i=0;i<discarded;i++) {
        PPPlatformEvent event=events[(first+i)%EVENT_CAPACITY];
        if(event.kind==PP_EVENT_CANCEL ||
            ((event.kind==PP_EVENT_BUTTON_DOWN || event.kind==PP_EVENT_BUTTON_UP) && event.button==PP_BUTTON_LEFT))
            append_cancellation(event);
    }
    if(owner && count<EVENT_CAPACITY) {
        PPPlatformEvent event=incoming;event.surface=owner;event.instance_id=owner->instance_id;
        append_cancellation(event);
    }
    if((incoming.kind==PP_EVENT_CANCEL || incoming.button==PP_BUTTON_LEFT) && count<EVENT_CAPACITY)
        append_cancellation(incoming);
    pp_platform_release_capture();
}
bool pp_windows_enqueue(PPPlatformEvent event) {
    if(event.kind==PP_EVENT_POINTER_MOVE && count) {
        unsigned last=(first+count-1)%EVENT_CAPACITY;
        if(events[last].kind==PP_EVENT_POINTER_MOVE && events[last].surface==event.surface) {events[last]=event;return true;}
    }
    if(count==EVENT_CAPACITY) {
        PPSurface *owner=pp_windows_input_owner();
        if(event.kind==PP_EVENT_POINTER_MOVE && event.surface!=owner) return true;
        /* Sacrifice hover before owned movement or gesture boundaries. All
         * work is bounded by the fixed queue; no allocation in hook callbacks. */
        discard_moves(owner);
        if(count==EVENT_CAPACITY) discard_moves(NULL);
        if(count==EVENT_CAPACITY) {cancel_discarded_gestures(event,owner);return false;}
    }
    events[(first+count++)%EVENT_CAPACITY]=event;return true;
}
PPPlatformEvent pp_windows_event(PPSurface *surface,UINT message,POINT point,DWORD wheel_data) {
    PPPlatformEvent event={0};event.surface=surface;event.instance_id=surface->instance_id;
    switch(message) {
    case WM_MOUSEMOVE:event.kind=PP_EVENT_POINTER_MOVE;break;
    case WM_LBUTTONDOWN:case WM_RBUTTONDOWN:case WM_MBUTTONDOWN:event.kind=PP_EVENT_BUTTON_DOWN;break;
    case WM_LBUTTONUP:case WM_RBUTTONUP:case WM_MBUTTONUP:event.kind=PP_EVENT_BUTTON_UP;break;
    case WM_MOUSEWHEEL:event.kind=PP_EVENT_WHEEL;event.wheel_delta=(SHORT)HIWORD(wheel_data)/(double)WHEEL_DELTA;break;
    default:event.kind=PP_EVENT_CANCEL;break;
    }
    if(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP) event.button=PP_BUTTON_LEFT;
    if(message==WM_RBUTTONDOWN||message==WM_RBUTTONUP) event.button=PP_BUTTON_RIGHT;
    if(message==WM_MBUTTONDOWN||message==WM_MBUTTONUP) event.button=PP_BUTTON_MIDDLE;
    event.desktop_point=(PPVec2){point.x,point.y};
    /* Async modifiers work while the receiver keeps keyboard focus. */
    if(GetAsyncKeyState(VK_SHIFT)<0) event.modifiers|=PP_MOD_SHIFT;
    if(GetAsyncKeyState(VK_CONTROL)<0) event.modifiers|=PP_MOD_CONTROL;
    if(GetAsyncKeyState(VK_MENU)<0) event.modifiers|=PP_MOD_ALT;
    LARGE_INTEGER counter,frequency;QueryPerformanceCounter(&counter);QueryPerformanceFrequency(&frequency);
    event.timestamp=(double)counter.QuadPart/(double)frequency.QuadPart;return event;
}
void pp_windows_record(PPSurface *surface,HWND window,UINT message,WPARAM w,LPARAM l) {
    (void)w;
    if(message==WM_CANCELMODE||message==WM_CAPTURECHANGED||message==WM_DESTROY||message==WM_DISPLAYCHANGE) {
        pp_windows_input_cancel(surface);return;
    }
    /* Hook owns button/wheel normalization, avoiding WndProc duplicates.
     * Unowned hover stays native; owned moves are recorded by the hook. */
    if(message!=WM_MOUSEMOVE || pp_windows_input_owned(surface)) return;
    POINT point={GET_X_LPARAM(l),GET_Y_LPARAM(l)};ClientToScreen(window,&point);
    pp_windows_enqueue(pp_windows_event(surface,message,point,0));
}
void pp_windows_forget(PPSurface *surface) {
    unsigned kept=0;
    for(unsigned i=0;i<count;i++) {
        PPPlatformEvent event=events[(first+i)%EVENT_CAPACITY];
        if(event.surface!=surface) events[(first+kept++)%EVENT_CAPACITY]=event;
    }
    count=kept;
}
bool pp_platform_poll(PPPlatformEvent *out) {
    if(!out) return false;
    MSG message;
    while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)) {
        if(message.message==WM_QUIT) {pp_platform_release_capture();*out=(PPPlatformEvent){0};out->kind=PP_EVENT_QUIT;return true;}
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    pp_windows_input_check();
    if(!count) return false;
    *out=events[first];first=(first+1)%EVENT_CAPACITY;count--;return true;
}
