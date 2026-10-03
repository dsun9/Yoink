#include "pointy/platform.h"
#include "pointy/interaction.h"
#include "receiver_shared.h"
#include "../test.h"
#include "../../src/platform/windows/windows_internal.h"
#include <math.h>
static PROCESS_INFORMATION child;
static ReceiverState *receiver;
static HANDLE mapping;
static PPSurface *surface;
static PPSurface *aux_surface;
static HWND old_foreground;
static POINT old_cursor;
static UINT old_routing;
static bool have_routing;
static bool alt_held;
static bool left_held;
static int downs,moves,ups,wheels,cancels;
static unsigned wheel_modifiers;
static double wheel_total;
static bool capture_on_down;
static PPInteraction *interaction_watch;
static PPGestureConfig interaction_config;
static void cleanup(void) {
    pp_surface_destroy(aux_surface);pp_surface_destroy(surface); pp_platform_release_capture();
    if(left_held) {INPUT input={0};input.type=INPUT_MOUSE;input.mi.dwFlags=MOUSEEVENTF_LEFTUP;SendInput(1,&input,sizeof(input));}
    if(alt_held) {INPUT input={0};input.type=INPUT_KEYBOARD;input.ki.wVk=VK_MENU;input.ki.dwFlags=KEYEVENTF_KEYUP;SendInput(1,&input,sizeof(input));}
    if(have_routing) SystemParametersInfoW(SPI_SETMOUSEWHEELROUTING,0,(void *)(UINT_PTR)old_routing,0);
    if(receiver && receiver->window) PostMessageW((HWND)receiver->window,WM_CLOSE,0,0);
    if(child.hProcess) {WaitForSingleObject(child.hProcess,3000);CloseHandle(child.hProcess);CloseHandle(child.hThread);}
    if(receiver) UnmapViewOfFile(receiver); if(mapping) CloseHandle(mapping);
    SetCursorPos(old_cursor.x,old_cursor.y);if(old_foreground) SetForegroundWindow(old_foreground);
}
static void pump(void) {
    ULONGLONG until=GetTickCount64()+100;
    do {PPPlatformEvent e;while(pp_platform_poll(&e)) {
        CHECK(e.surface==surface);CHECK(e.instance_id==42);
        if(interaction_watch) {PPInteractionResult out;pp_interaction_step(interaction_watch,&e,&interaction_config,&out);}
        if(e.kind==PP_EVENT_BUTTON_DOWN) {downs++;if(capture_on_down) {PPError error={0};CHECK(pp_platform_capture(surface,&error));}}
        if(e.kind==PP_EVENT_POINTER_MOVE) moves++;
        if(e.kind==PP_EVENT_BUTTON_UP) ups++;
        if(e.kind==PP_EVENT_WHEEL) {wheels++;wheel_total+=e.wheel_delta;wheel_modifiers=e.modifiers;}
        if(e.kind==PP_EVENT_CANCEL) cancels++;
    }Sleep(1);}while(GetTickCount64()<until);
}
typedef struct Injection {bool move;int x,y;DWORD flags,data;bool ok;WORD key;INPUT *batch;UINT count;} Injection;
static DWORD WINAPI inject(void *context) {
    Injection *request=context;
    if(request->batch) {
        request->ok=true;
        for(UINT i=0;i<request->count;i++) {
            if(SendInput(1,&request->batch[i],sizeof(INPUT))!=1) request->ok=false;
            Sleep(10);
        }
        return 0;
    }
    INPUT input={0};input.type=request->key?INPUT_KEYBOARD:INPUT_MOUSE;
    if(request->key) {input.ki.wVk=request->key;input.ki.dwFlags=request->flags;}
    else if(request->move) {
        input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
        input.mi.dx=MulDiv(request->x-GetSystemMetrics(SM_XVIRTUALSCREEN),65535,GetSystemMetrics(SM_CXVIRTUALSCREEN)-1);
        input.mi.dy=MulDiv(request->y-GetSystemMetrics(SM_YVIRTUALSCREEN),65535,GetSystemMetrics(SM_CYVIRTUALSCREEN)-1);
    } else {input.mi.dwFlags=request->flags;input.mi.mouseData=request->data;}
    request->ok=SendInput(1,&input,sizeof(input))==1;
    return 0;
}
static void injection(Injection *request) {
    HANDLE thread=CreateThread(NULL,0,inject,request,0,NULL);CHECK(thread);
    ULONGLONG until=GetTickCount64()+5000;
    while(WaitForSingleObject(thread,0)==WAIT_TIMEOUT && GetTickCount64()<until) {
        if(request->batch) {
            /* Deliver hook/WndProc work, defer caller consumption of the queue. */
            MSG message;while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
            Sleep(1);
        } else pump();
    }
    CHECK(WaitForSingleObject(thread,0)==WAIT_OBJECT_0);CloseHandle(thread);CHECK(request->ok);pump();
}
static void move(int x,int y) {Injection request={true,x,y,0,0,false};injection(&request);}
static void mouse(DWORD flags,DWORD data) {
    if(flags&MOUSEEVENTF_LEFTDOWN) left_held=true;
    Injection request={false,0,0,flags,data,false};injection(&request);
    if(flags&MOUSEEVENTF_LEFTUP) left_held=false;
}
static void key(WORD value,DWORD flags) {
    if(value==VK_MENU && !(flags&KEYEVENTF_KEYUP)) alt_held=true;
    Injection request={false,0,0,flags,0,false,value};injection(&request);
    if(value==VK_MENU && (flags&KEYEVENTF_KEYUP)) alt_held=false;
}
static void click(int x,int y) {move(x,y);mouse(MOUSEEVENTF_LEFTDOWN,0);mouse(MOUSEEVENTF_LEFTUP,0);}
static PPFrame ring(void) {
    PPFrame f={0};f.width=f.height=64;f.stride=256;f.pixels=calloc(64,256);f.content_alpha=calloc(64,64);CHECK(f.pixels&&f.content_alpha);
    for(int y=0;y<64;y++) for(int x=0;x<64;x++) if(hypot(x-31.5,y-31.5)>=18&&hypot(x-31.5,y-31.5)<=29) {
        f.pixels[(y*64+x)*4]=255;f.pixels[(y*64+x)*4+3]=255;f.content_alpha[y*64+x]=255;
    }return f;
}
static void drain_interaction(PPInteraction *state,bool *cancel_a) {
    PPPlatformEvent event;
    while(pp_platform_poll(&event)) {
        if(event.kind==PP_EVENT_CANCEL && event.instance_id==42) *cancel_a=true;
        PPInteractionResult out;pp_interaction_step(state,&event,&interaction_config,&out);
    }
}
static void test_queue_exhaustion_preserves_owner(void) {
    PPError error={0};PPSurface *other=NULL;CHECK(pp_surface_create(&other,&error));
    aux_surface=other;
    pp_surface_set_instance_id(other,43);pp_platform_gesture_config(&interaction_config);
    PPInteraction state={0};state.displayed_pose=(PPPose){{252,252},64,0,1};interaction_watch=&state;
    move(277,252);mouse(MOUSEEVENTF_LEFTDOWN,0);move(430,350);CHECK(state.dragging);
    /* A hook move and B's unowned WndProc hover bypass adjacent coalescing. */
    for(int i=0;i<512;i++) {
        if(i%2) CHECK(pp_windows_enqueue(pp_windows_event(surface,WM_MOUSEMOVE,(POINT){431+i%3,350},0)));
        else SendMessageW(other->window,WM_MOUSEMOVE,0,MAKELPARAM(1,1));
    }
    SendMessageW(other->window,WM_MOUSEMOVE,0,MAKELPARAM(2,1));
    CHECK(pp_platform_capture(surface,&error)); /* Expendable hover must not revoke A. */
    bool cancelled=false;drain_interaction(&state,&cancelled);CHECK(!cancelled && state.dragging);
    mouse(MOUSEEVENTF_LEFTUP,0);CHECK(!state.pressed && !state.dragging);

    state=(PPInteraction){0};state.displayed_pose=(PPPose){{252,252},64,0,1};
    move(277,252);mouse(MOUSEEVENTF_LEFTDOWN,0);move(430,350);CHECK(state.dragging);
    /* Native ownership may end before its queued release reaches the core. */
    pp_platform_release_capture();
    CHECK(pp_windows_enqueue(pp_windows_event(surface,WM_LBUTTONUP,(POINT){430,350},0)));
    for(int i=0;i<511;i++) CHECK(pp_windows_enqueue(pp_windows_event(other,WM_MOUSEWHEEL,(POINT){1,1},(DWORD)WHEEL_DELTA<<16)));
    pp_windows_enqueue(pp_windows_event(other,WM_MOUSEWHEEL,(POINT){1,1},(DWORD)WHEEL_DELTA<<16));
    cancelled=false;drain_interaction(&state,&cancelled);
    CHECK(cancelled && !state.pressed && !state.dragging);
    mouse(MOUSEEVENTF_LEFTUP,0);interaction_watch=NULL;pp_surface_destroy(other);aux_surface=NULL;
    puts("PASS overflow drops expendable hover and cancels A's discarded queued release even after native ownership ended.");
}
static void test_capture_and_wheel(void) {
    PPError error={0};PPFrame frame=ring();CHECK(pp_surface_create(&surface,&error));
    PPGestureConfig config={0};pp_platform_gesture_config(&config);
    CHECK(config.drag_x==GetSystemMetrics(SM_CXDRAG)*.5 && config.drag_y==GetSystemMetrics(SM_CYDRAG)*.5);
    CHECK(config.double_click_seconds==GetDoubleClickTime()/1000.0);
    pp_surface_set_instance_id(surface,42);CHECK(pp_surface_present(surface,&frame,(PPRect){220,220,64,64},&error));pump();
    CHECK(!pp_platform_capture(surface,&error)); /* Capture needs an accepted sprite press. */
    for(int setting=0;setting<2;setting++) {
        UINT route=setting?MOUSEWHEEL_ROUTING_MOUSE_POS:MOUSEWHEEL_ROUTING_FOCUS;
        CHECK(SystemParametersInfoW(SPI_SETMOUSEWHEELROUTING,0,(void *)(UINT_PTR)route,0));
        LONG receiver_wheels=receiver->wheels;int sprite_wheels=wheels;
        move(277,252);mouse(MOUSEEVENTF_WHEEL,WHEEL_DELTA);
        CHECK(wheels==sprite_wheels+1 && receiver->wheels==receiver_wheels);
        move(252,252);mouse(MOUSEEVENTF_WHEEL,WHEEL_DELTA);
        CHECK(wheels==sprite_wheels+1 && receiver->wheels==receiver_wheels+1);
        /* Current native z-order wins over the sprite's stored geometry. */
        CHECK(SetWindowPos((HWND)receiver->window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE));
        move(277,252);mouse(MOUSEEVENTF_WHEEL,WHEEL_DELTA);
        CHECK(wheels==sprite_wheels+1 && receiver->wheels==receiver_wheels+2);
        CHECK(SetWindowPos((HWND)receiver->window,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE));
        CHECK(pp_surface_present(surface,&frame,(PPRect){220,220,64,64},&error));
        /* A stationary rim pixel disappears; its next wheel belongs below. */
        frame.pixels[(32*64+57)*4+3]=0;frame.content_alpha[32*64+57]=0;
        CHECK(pp_surface_present(surface,&frame,(PPRect){220,220,64,64},&error));
        mouse(MOUSEEVENTF_WHEEL,WHEEL_DELTA);
        CHECK(wheels==sprite_wheels+1 && receiver->wheels==receiver_wheels+3);
        frame.pixels[(32*64+57)*4+3]=255;frame.content_alpha[32*64+57]=255;
        CHECK(pp_surface_present(surface,&frame,(PPRect){220,220,64,64},&error));
        CHECK(GetForegroundWindow()==(HWND)receiver->window);
    }
    CHECK(wheel_total==2);
    move(277,252);key(VK_MENU,0);mouse(MOUSEEVENTF_WHEEL,WHEEL_DELTA/2);
    CHECK(wheel_total==2.5 && (wheel_modifiers&PP_MOD_ALT));
    key(VK_MENU,KEYEVENTF_KEYUP);key(VK_ESCAPE,0);key(VK_ESCAPE,KEYEVENTF_KEYUP);
    /* The OS can deliver the full press/move/release before the caller pops
     * the down event. Accepted native presses must retain that trajectory. */
    move(277,252);int batch_moves=moves,batch_ups=ups;
    INPUT batch[3]={0};for(int i=0;i<3;i++) batch[i].type=INPUT_MOUSE;
    batch[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
    batch[1].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
    batch[1].mi.dx=MulDiv(430-GetSystemMetrics(SM_XVIRTUALSCREEN),65535,GetSystemMetrics(SM_CXVIRTUALSCREEN)-1);
    batch[1].mi.dy=MulDiv(350-GetSystemMetrics(SM_YVIRTUALSCREEN),65535,GetSystemMetrics(SM_CYVIRTUALSCREEN)-1);
    batch[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    Injection request={0};request.batch=batch;request.count=3;injection(&request);
    CHECK(moves==batch_moves+1 && ups==batch_ups+1);
    capture_on_down=true;move(277,252);mouse(MOUSEEVENTF_LEFTDOWN,0);
    int old_moves=moves;move(430,350);CHECK(moves>old_moves);
    int old_ups=ups;mouse(MOUSEEVENTF_LEFTUP,0);CHECK(ups==old_ups+1);
    CHECK(GetForegroundWindow()==(HWND)receiver->window);
    LONG receiver_clicks=receiver->clicks;click(430,350);CHECK(receiver->clicks==receiver_clicks+1);
    move(277,252);mouse(MOUSEEVENTF_LEFTDOWN,0);
    HWND native=FindWindowW(L"PointyPointer.Surface",NULL);CHECK(native);
    int old_cancels=cancels;SendMessageW(native,WM_CANCELMODE,0,0);pump();CHECK(cancels==old_cancels+1);
    mouse(MOUSEEVENTF_LEFTUP,0);receiver_clicks=receiver->clicks;click(430,350);CHECK(receiver->clicks==receiver_clicks+1);
    move(277,252);mouse(MOUSEEVENTF_LEFTDOWN,0);old_cancels=cancels;
    SendMessageW(native,WM_CAPTURECHANGED,0,(LPARAM)receiver->window);pump();CHECK(cancels==old_cancels+1);
    mouse(MOUSEEVENTF_LEFTUP,0);receiver_clicks=receiver->clicks;click(430,350);CHECK(receiver->clicks==receiver_clicks+1);
    move(277,252);mouse(MOUSEEVENTF_LEFTDOWN,0);
    pp_surface_destroy(surface);surface=NULL;mouse(MOUSEEVENTF_LEFTUP,0);
    receiver_clicks=receiver->clicks;click(252,252);CHECK(receiver->clicks==receiver_clicks+1);
    CHECK(GetCapture()==NULL);CHECK(GetForegroundWindow()==(HWND)receiver->window);
    /* Last-surface destruction removes the hook; a new surface installs it. */
    CHECK(pp_surface_create(&surface,&error));pp_surface_set_instance_id(surface,42);
    CHECK(pp_surface_present(surface,&frame,(PPRect){220,220,64,64},&error));
    move(277,252);int old_wheels=wheels;mouse(MOUSEEVENTF_WHEEL,WHEEL_DELTA);
    CHECK(wheels==old_wheels+1);
    CHECK(GetForegroundWindow()==(HWND)receiver->window);
    test_queue_exhaustion_preserves_owner();
    pp_surface_destroy(surface);surface=NULL;
    pp_frame_destroy(&frame);puts("PASS owned drag outside surface, foreground preservation, cancellation/destruction cleanup, wheel rim/hole with both OS routing modes.");
}
int main(int argc,char **argv) {
    CHECK(argc==2);old_foreground=GetForegroundWindow();GetCursorPos(&old_cursor);atexit(cleanup);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    have_routing=SystemParametersInfoW(SPI_GETMOUSEWHEELROUTING,0,&old_routing,0)!=FALSE;CHECK(have_routing);
    char name[80];snprintf(name,sizeof(name),"Local\\PointyCapture-%lu",GetCurrentProcessId());
    mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(ReceiverState),name);CHECK(mapping);
    receiver=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(*receiver));CHECK(receiver);
    char command[2048];snprintf(command,sizeof(command),"\"%s\" %s",argv[1],name);STARTUPINFOA startup={sizeof(startup)};
    CHECK(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&child));
    ULONGLONG until=GetTickCount64()+5000;while(!receiver->window&&GetTickCount64()<until) Sleep(10);
    CHECK(receiver->window);CHECK(SetForegroundWindow((HWND)receiver->window));
    test_capture_and_wheel();return 0;
}
