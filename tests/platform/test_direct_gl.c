/* Opt-in native feasibility gate. Desktop observations come from the real
 * receiver, OS input injection, foreground/window order and composited pixels. */
#include "pointy/platform.h"
#include "receiver_shared.h"
#include "../test.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <math.h>
#include <string.h>
#define API(expr) do {bool ok=(expr);if(!ok) fprintf(stderr,"API failure: %s\n",error.message);CHECK(ok);} while(0)
static PPError error;
static PROCESS_INFORMATION child;
static ReceiverState *receiver;
static HANDLE mapping;
static PPSurface *surfaces[2];
static PPRenderer *renderers[2];
static SDL_Window *settings,*drawer;
static HWND saved_foreground;
static POINT saved_cursor;
static bool left_held,right_held,capture_on_down;
typedef struct Observation {int downs,ups,moves,wheels,right_downs,right_ups,cancels;} Observation;
static Observation observations[2];
static void cleanup(void) {
    pp_platform_settings_window(0);pp_platform_drawer_close(false);
    SDL_DestroyWindow(drawer);SDL_DestroyWindow(settings);
    for(int i=1;i>=0;i--) {pp_renderer_destroy(renderers[i]);pp_surface_destroy(surfaces[i]);}
    INPUT up={.type=INPUT_MOUSE};up.mi.dwFlags=MOUSEEVENTF_LEFTUP|MOUSEEVENTF_RIGHTUP;
    if(left_held||right_held) SendInput(1,&up,sizeof(up));
    pp_platform_release_capture();
    if(receiver&&receiver->window) PostMessageW((HWND)receiver->window,WM_CLOSE,0,0);
    if(child.hProcess) {
        if(WaitForSingleObject(child.hProcess,3000)!=WAIT_OBJECT_0) TerminateProcess(child.hProcess,1);
        CloseHandle(child.hProcess);CloseHandle(child.hThread);
    }
    if(receiver) UnmapViewOfFile(receiver);if(mapping) CloseHandle(mapping);
    SetCursorPos(saved_cursor.x,saved_cursor.y);
    if(saved_foreground) SetForegroundWindow(saved_foreground);
}
static void poll(void) {
    PPPlatformEvent event;
    while(pp_platform_poll(&event)) {
        int i=-1;for(int j=0;j<2;j++) if(surfaces[j] && event.surface==surfaces[j]) i=j;
        if(i<0) continue;
        CHECK(event.instance_id==(uint64_t)(42+i));
        Observation *o=&observations[i];
        if(event.kind==PP_EVENT_BUTTON_DOWN) {
            if(event.button==PP_BUTTON_RIGHT) o->right_downs++;
            if(event.button==PP_BUTTON_LEFT) {o->downs++;if(capture_on_down) API(pp_platform_capture(surfaces[i],&error));}
        }
        if(event.kind==PP_EVENT_BUTTON_UP) {
            if(event.button==PP_BUTTON_LEFT) o->ups++;
            if(event.button==PP_BUTTON_RIGHT) o->right_ups++;
        }
        if(event.kind==PP_EVENT_POINTER_MOVE) o->moves++;
        if(event.kind==PP_EVENT_WHEEL) o->wheels++;
        if(event.kind==PP_EVENT_CANCEL) o->cancels++;
    }
}
static void pump(void) {
    ULONGLONG until=GetTickCount64()+70;
    do {poll();Sleep(1);} while(GetTickCount64()<until);
}
typedef struct Injection {INPUT input;UINT sent;} Injection;
static DWORD WINAPI inject(void *context) {
    Injection *request=context;request->sent=SendInput(1,&request->input,sizeof(INPUT));return 0;
}
static void send_injection(Injection *request) {
    HANDLE thread=CreateThread(NULL,0,inject,request,0,NULL);CHECK(thread);
    ULONGLONG until=GetTickCount64()+3000;
    while(WaitForSingleObject(thread,0)==WAIT_TIMEOUT && GetTickCount64()<until) {poll();Sleep(1);}
    CHECK(WaitForSingleObject(thread,0)==WAIT_OBJECT_0);CloseHandle(thread);CHECK(request->sent==1);pump();
}
static void move(int x,int y) {
    Injection request={0};request.input.type=INPUT_MOUSE;
    request.input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
    request.input.mi.dx=MulDiv(x-GetSystemMetrics(SM_XVIRTUALSCREEN),65535,GetSystemMetrics(SM_CXVIRTUALSCREEN)-1);
    request.input.mi.dy=MulDiv(y-GetSystemMetrics(SM_YVIRTUALSCREEN),65535,GetSystemMetrics(SM_CYVIRTUALSCREEN)-1);
    send_injection(&request);
}
static void mouse(DWORD flags,DWORD data) {
    if(flags&MOUSEEVENTF_LEFTDOWN) left_held=true;
    if(flags&MOUSEEVENTF_RIGHTDOWN) right_held=true;
    Injection request={0};request.input.type=INPUT_MOUSE;request.input.mi.dwFlags=flags;request.input.mi.mouseData=data;
    send_injection(&request);
    if(flags&MOUSEEVENTF_LEFTUP) left_held=false;
    if(flags&MOUSEEVENTF_RIGHTUP) right_held=false;
}
static void click(int x,int y) {
    /* No hover/pump delay between arrival and first press. */
    CHECK(SetCursorPos(x,y));mouse(MOUSEEVENTF_LEFTDOWN,0);mouse(MOUSEEVENTF_LEFTUP,0);
}
static HWND native(int i) {
    SDL_Window *window=SDL_GetWindowFromID(pp_surface_window_id(surfaces[i]));CHECK(window);
    return SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,NULL);
}
static PPFrame ring(void) {
    PPFrame frame={0};frame.width=frame.height=64;frame.stride=256;frame.pixels=calloc(64,256);CHECK(frame.pixels);
    for(int y=0;y<64;y++) for(int x=0;x<64;x++) if(hypot(x-31.5,y-31.5)>=18 && hypot(x-31.5,y-31.5)<=29) {
        size_t p=((size_t)y*64+x)*4;frame.pixels[p]=255;frame.pixels[p+3]=255;
    }
    for(int y=29;y<=34;y++) for(int x=51;x<=54;x++) {
        size_t p=((size_t)y*64+x)*4;frame.pixels[p]=128;frame.pixels[p+3]=128;
    }
    return frame;
}
static void create(int i,const PPFrame *source) {
    API(pp_surface_create_gl(&surfaces[i],&error));pp_surface_set_instance_id(surfaces[i],42+i);
    API(pp_renderer_create_for_window(&renderers[i],pp_surface_window_id(surfaces[i]),&error));
    API(pp_renderer_upload_source(renderers[i],source,42+i,1,&error));
}
static void present(int i,double angle,double opacity,PPRect bounds) {
    API(pp_surface_place_gl(surfaces[i],bounds,opacity>0,&error));
    if(opacity>0) {
        API(pp_renderer_draw(renderers[i],(PPVec2){64,64},64,angle,opacity,1,&error));
        API(pp_renderer_complete(renderers[i],&error));
    }
    pump();
}
static COLORREF pixel(int x,int y) {
    HDC screen=GetDC(NULL);CHECK(screen);COLORREF value=GetPixel(screen,x,y);ReleaseDC(NULL,screen);CHECK(value!=CLR_INVALID);return value;
}
static void expect_pixel(const char *name,int x,int y,int r,int g,int b) {
    COLORREF value=pixel(x,y);
    printf("Compositor pixel %s at %d,%d: RGB=%d,%d,%d expected=%d,%d,%d tolerance=3\n",
        name,x,y,GetRValue(value),GetGValue(value),GetBValue(value),r,g,b);
    CHECK(abs((int)GetRValue(value)-r)<=3 && abs((int)GetGValue(value)-g)<=3 && abs((int)GetBValue(value)-b)<=3);
}
static void test_alpha_and_rectangle(PPFrame *source) {
    create(0,source);
    printf("Backend SDL=%s driver=%s GL vendor=%s renderer=%s version=%s\n",SDL_GetRevision(),SDL_GetCurrentVideoDriver(),glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
    present(0,0,1,(PPRect){220,220,64,64});
    CHECK(GetForegroundWindow()==(HWND)receiver->window);
    expect_pixel("transparent interior",252,252,255,255,255);
    expect_pixel("padding",221,221,255,255,255);
    expect_pixel("opaque rim",277,252,255,0,0);
    expect_pixel("premultiplied patch",272,252,255,127,127);
    present(0,0,.5,(PPRect){220,220,64,64});
    expect_pixel("whole sprite opacity",277,252,255,128,128);
    expect_pixel("opacity preserves transparent interior",252,252,255,255,255);
    present(0,0,1,(PPRect){220,220,64,64});
    LONG below=receiver->clicks;int before=observations[0].downs;
    click(252,252);click(221,221);click(277,252);
    CHECK(observations[0].downs==before+3 && receiver->clicks==below);
    CHECK(GetForegroundWindow()==(HWND)receiver->window);
    click(219,252);CHECK(receiver->clicks==below+1 && observations[0].downs==before+3);
    CHECK(SetCursorPos(252,252));int wheels=observations[0].wheels;LONG outside_wheels=receiver->wheels;
    mouse(MOUSEEVENTF_WHEEL,WHEEL_DELTA);CHECK(observations[0].wheels==wheels+1 && receiver->wheels==outside_wheels);
    CHECK(SetCursorPos(219,252));mouse(MOUSEEVENTF_WHEEL,WHEEL_DELTA);CHECK(receiver->wheels==outside_wheels+1);
    present(0,37,1,(PPRect){220,220,90,90});
    expect_pixel("rotated interior",265,265,255,255,255);
    expect_pixel("empty rotated corner",221,221,255,255,255);
    before=observations[0].downs;below=receiver->clicks;
    click(221,221);click(265,265);CHECK(observations[0].downs==before+2 && receiver->clicks==below);
    puts("PASS visible alpha, premultiplication, first-click rectangle at hole/padding/rotated corner, outside input and wheel");
}
static void destroy(int i) {
    pp_renderer_destroy(renderers[i]);renderers[i]=NULL;
    pp_surface_destroy(surfaces[i]);surfaces[i]=NULL;
}
static void test_capture_and_cleanup(PPFrame *source) {
    present(0,0,1,(PPRect){220,220,64,64});
    CHECK(!pp_platform_capture(surfaces[0],&error));
    create(1,source);present(1,0,1,(PPRect){330,220,64,64});
    CHECK(GetForegroundWindow()==(HWND)receiver->window);
    capture_on_down=true;CHECK(SetCursorPos(252,252));
    int before=observations[0].downs,ups=observations[0].ups,moves=observations[0].moves;
    LONG below=receiver->clicks;mouse(MOUSEEVENTF_LEFTDOWN,0);
    CHECK(!pp_platform_capture(surfaces[1],&error));
    API(pp_platform_capture(surfaces[0],&error));
    move(430,350);mouse(MOUSEEVENTF_LEFTUP,0);
    CHECK(observations[0].downs==before+1 && observations[0].ups==ups+1 && observations[0].moves>moves);
    CHECK(receiver->clicks==below && !pp_platform_capture(surfaces[0],&error));
    if(GetForegroundWindow()!=(HWND)receiver->window) {
        wchar_t title[128]={0};GetWindowTextW(GetForegroundWindow(),title,128);
        fprintf(stderr,"Capture foreground changed to %p '%ls', receiver=%p\n",GetForegroundWindow(),title,(HWND)receiver->window);
    }
    CHECK(GetForegroundWindow()==(HWND)receiver->window);
    destroy(1);
    int right_downs=observations[0].right_downs,right_ups=observations[0].right_ups;
    LONG receiver_right=receiver->right_ups;
    CHECK(SetCursorPos(252,252));mouse(MOUSEEVENTF_RIGHTDOWN,0);mouse(MOUSEEVENTF_RIGHTUP,0);
    CHECK(observations[0].right_downs==right_downs+1 && observations[0].right_ups==right_ups+1 && receiver->right_ups==receiver_right);
    puts("PASS accepted-press ownership, drag trajectory and release outside bounds, capture release and nonactivation");
    for(int variant=0;variant<4;variant++) {
        if(!surfaces[0]) create(0,source);
        present(0,0,1,(PPRect){220,220,64,64});CHECK(SetCursorPos(252,252));
        mouse(MOUSEEVENTF_LEFTDOWN,0);API(pp_platform_capture(surfaces[0],&error));
        if(variant==0) present(0,0,0,(PPRect){220,220,64,64});
        if(variant==1) pp_surface_hide(surfaces[0]);
        if(variant==2) CHECK(!pp_surface_place_gl(surfaces[0],(PPRect){NAN,220,64,64},true,&error));
        if(variant==3) destroy(0);
        pump();CHECK(!pp_platform_capture(surfaces[0],&error));
        mouse(MOUSEEVENTF_LEFTUP,0);capture_on_down=false;
        before=observations[0].downs;below=receiver->clicks;
        /* Pointer stays at the same transparent interior during visibility change. */
        mouse(MOUSEEVENTF_LEFTDOWN,0);mouse(MOUSEEVENTF_LEFTUP,0);
        CHECK(receiver->clicks==below+1 && observations[0].downs==before);
        CHECK(GetForegroundWindow()==(HWND)receiver->window);
        printf("PASS stationary-pointer capture cleanup variant=%d (%s), next click reaches receiver\n",variant,
            variant==0?"zero opacity":variant==1?"hidden":variant==2?"failed placement":"destroyed");
        capture_on_down=true;
    }
    capture_on_down=false;
    for(int variant=0;variant<4;variant++) {
        create(0,source);present(0,0,1,(PPRect){220,220,64,64});CHECK(SetCursorPos(252,252));
        LONG downs=receiver->right_clicks,release=receiver->right_ups,context=receiver->context_menus;
        int right=observations[0].right_downs;mouse(MOUSEEVENTF_RIGHTDOWN,0);
        CHECK(observations[0].right_downs==right+1);
        if(variant==0) pp_surface_hide(surfaces[0]);
        if(variant==1) present(0,0,0,(PPRect){220,220,64,64});
        if(variant==2) CHECK(!pp_surface_place_gl(surfaces[0],(PPRect){220,220,0,64},true,&error));
        if(variant==3) destroy(0);
        CHECK(SetCursorPos(430,350));mouse(MOUSEEVENTF_RIGHTUP,0);
        CHECK(receiver->right_clicks==downs && receiver->right_ups==release && receiver->context_menus==context);
        destroy(0);
        mouse(MOUSEEVENTF_RIGHTDOWN,0);mouse(MOUSEEVENTF_RIGHTUP,0);
        CHECK(receiver->right_clicks==downs+1 && receiver->right_ups==release+1 && receiver->context_menus==context+1);
        printf("PASS consumed right release after variant=%d, moved outside, subsequent original right gesture reaches receiver\n",variant);
    }
}
static HWND sdl_native(SDL_Window *window) {
    return SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,NULL);
}
static SDL_Window *ui_fixture(const char *title) {
    SDL_Window *window=SDL_CreateWindow(title,80,80,SDL_WINDOW_HIDDEN|SDL_WINDOW_BORDERLESS|SDL_WINDOW_NOT_FOCUSABLE|SDL_WINDOW_ALWAYS_ON_TOP);
    CHECK(window);CHECK(SDL_SetWindowPosition(window,240,240));CHECK(SDL_ShowWindow(window));pump();return window;
}
static void test_order_and_reuse(PPFrame *source) {
    create(0,source);present(0,0,1,(PPRect){220,220,64,64});
    create(1,source);present(1,0,1,(PPRect){230,230,64,64});
    int a=observations[0].downs,b=observations[1].downs;click(252,252);
    CHECK(observations[1].downs==b+1 && observations[0].downs==a);
    pp_surface_raise(surfaces[0]);pump();click(252,252);CHECK(observations[0].downs==a+1);
    present(1,0,1,(PPRect){230,230,64,64});CHECK(WindowFromPoint((POINT){252,252})==native(0));
    PPRendererMetrics before=pp_renderer_metrics(renderers[0]);
    HWND retained=native(0);
    API(pp_surface_place_gl(surfaces[0],(PPRect){222,222,64,64},true,&error));pump();
    PPRendererMetrics after=pp_renderer_metrics(renderers[0]);
    CHECK(native(0)==retained && before.source_uploads==after.source_uploads && before.transform_draws==after.transform_draws && !after.readbacks);
    API(pp_renderer_upload_source(renderers[0],source,42,1,&error));
    CHECK(pp_renderer_metrics(renderers[0]).source_uploads==before.source_uploads);
    /* Change color under a stationary pointer while preserving rectangular ownership. */
    CHECK(SetCursorPos(254,254));size_t p=((size_t)32*64+32)*4;
    source->pixels[p]=0;source->pixels[p+1]=255;source->pixels[p+2]=0;source->pixels[p+3]=255;
    API(pp_renderer_upload_source(renderers[0],source,42,2,&error));
    present(0,0,1,(PPRect){222,222,64,64});expect_pixel("stationary content revision",254,254,0,255,0);
    a=observations[0].downs;mouse(MOUSEEVENTF_LEFTDOWN,0);mouse(MOUSEEVENTF_LEFTUP,0);CHECK(observations[0].downs==a+1);
    CHECK(pp_renderer_metrics(renderers[0]).source_uploads==before.source_uploads+1);
    memset(source->pixels+p,0,4);
    puts("PASS overlapping sprite raise, redraw order retention, native movement with zero draws/uploads/readbacks, content revision at stationary pointer");
    settings=ui_fixture("PointyPointer Settings priority fixture");pp_platform_settings_window(SDL_GetWindowID(settings));
    drawer=ui_fixture("PointyPointer drawer priority fixture");pp_platform_drawer_window(SDL_GetWindowID(drawer));
    /* Newly shown sprites go below both registered application UI windows. */
    pp_surface_hide(surfaces[0]);present(0,0,1,(PPRect){222,222,64,64});
    CHECK(WindowFromPoint((POINT){254,254})==sdl_native(drawer));
    pp_surface_raise(surfaces[0]);CHECK(WindowFromPoint((POINT){254,254})==sdl_native(drawer));
    pp_platform_drawer_close(false);CHECK(WindowFromPoint((POINT){254,254})==sdl_native(settings));
    pp_surface_raise(surfaces[1]);CHECK(WindowFromPoint((POINT){254,254})==sdl_native(settings));
    SDL_HideWindow(settings);pp_platform_settings_window(0);
    SDL_DestroyWindow(drawer);drawer=NULL;SDL_DestroyWindow(settings);settings=NULL;
    CHECK(GetForegroundWindow()==(HWND)receiver->window);
    CHECK(SetWindowPos((HWND)receiver->window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE));
    a=observations[0].downs;b=observations[1].downs;LONG below=receiver->clicks;
    click(254,254);CHECK(receiver->clicks==below+1 && observations[0].downs==a && observations[1].downs==b);
    CHECK(SetWindowPos((HWND)receiver->window,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE));
    puts("PASS Settings/drawer registered native priority and receiver above sprites, keyboard foreground retained");
}
static void test_canonical_layout(void) {
    uint8_t pixels[]={255,0,0,255,0,255,0,255};PPFrame source={pixels,NULL,2,1,8};
    destroy(1);pp_surface_hide(surfaces[0]);
    API(pp_renderer_upload_source(renderers[0],&source,99,1,&error));
    API(pp_surface_place_gl(surfaces[0],(PPRect){220,220,64,64},true,&error));
    API(pp_renderer_draw(renderers[0],(PPVec2){1,1},64,0,1,1,&error));
    API(pp_renderer_complete(renderers[0],&error));pump();
    expect_pixel("canonical square from 2:1 cached raster left",225,280,255,0,0);
    expect_pixel("canonical square from 2:1 cached raster right",278,280,0,255,0);
    PPRendererMetrics metrics=pp_renderer_metrics(renderers[0]);
    CHECK(metrics.source_uploads==3 && metrics.readbacks==0);
    puts("PASS canonical layout independently controls native bounds and mapping across raster aspect changes");
}
static void test_grab_region(PPFrame *source) {
    create(0,source);present(0,0,1,(PPRect){220,220,64,64});
    PPGrabArea *area=NULL;PPGrabRegion region={0};API(pp_grab_create(source,1,&area,&error));
    API(pp_grab_project(area,(PPVec2){64,64},(PPRect){0,0,64,64},64,0,1,64,64,&region,&error));
    API(pp_surface_set_grab_region(surfaces[0],&region,&error));pump();
    int before=observations[0].downs;LONG below=receiver->clicks;
    click(252,252);click(221,221);CHECK(observations[0].downs==before&&receiver->clicks==below+2);
    click(277,252);CHECK(observations[0].downs==before+1&&receiver->clicks==below+2);
    expect_pixel("shaped opaque rim",277,252,255,0,0);expect_pixel("shaped alpha patch",272,252,255,127,127);
    int wheels=observations[0].wheels;LONG lower_wheels=receiver->wheels;
    CHECK(SetCursorPos(252,252));mouse(MOUSEEVENTF_WHEEL,WHEEL_DELTA);CHECK(receiver->wheels==lower_wheels+1&&observations[0].wheels==wheels);
    CHECK(SetCursorPos(277,252));mouse(MOUSEEVENTF_WHEEL,WHEEL_DELTA);CHECK(observations[0].wheels==wheels+1);
    int right=observations[0].right_downs;LONG lower_right=receiver->right_clicks;
    CHECK(SetCursorPos(252,252));mouse(MOUSEEVENTF_RIGHTDOWN,0);mouse(MOUSEEVENTF_RIGHTUP,0);CHECK(receiver->right_clicks==lower_right+1&&observations[0].right_downs==right);
    capture_on_down=true;CHECK(SetCursorPos(277,252));int ups=observations[0].ups;
    mouse(MOUSEEVENTF_LEFTDOWN,0);API(pp_platform_capture(surfaces[0],&error));move(252,252);move(430,350);mouse(MOUSEEVENTF_LEFTUP,0);
    CHECK(observations[0].ups==ups+1&&GetForegroundWindow()==(HWND)receiver->window);capture_on_down=false;
    present(0,37,1,(PPRect){220,220,90,90});
    API(pp_grab_project(area,(PPVec2){64,64},(PPRect){0,0,64,64},64,37,1,90,90,&region,&error));
    API(pp_surface_set_grab_region(surfaces[0],&region,&error));pump();
    before=observations[0].downs;below=receiver->clicks;click(221,221);click(265,265);CHECK(receiver->clicks==below+2&&observations[0].downs==before);
    /* Lower sprite remains a rectangle in this fixture. Top ring's hole must
     * expose it using native order, without a synthetic replay. */
    create(1,source);present(1,37,1,(PPRect){220,220,90,90});pp_surface_raise(surfaces[0]);pump();
    int lower=observations[1].downs;click(265,265);CHECK(observations[1].downs==lower+1&&observations[0].downs==before);
    destroy(1);pp_grab_release(area);
    puts("PASS native quadtree hole/padding/corner first-click and wheel/right pass-through, retained rim pixels, capture outside shape, overlap order and nonactivation");
}
int main(int argc,char **argv) {
    CHECK(argc==2||argc==3);setvbuf(stdout,NULL,_IONBF,0);
    saved_foreground=GetForegroundWindow();CHECK(GetCursorPos(&saved_cursor));atexit(cleanup);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    char name[80];snprintf(name,sizeof(name),"Local\\PointyDirectReceiver-%lu",GetCurrentProcessId());
    mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(ReceiverState),name);CHECK(mapping);
    receiver=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(*receiver));CHECK(receiver);
    char command[2048];snprintf(command,sizeof(command),"\"%s\" %s",argv[1],name);STARTUPINFOA startup={sizeof(startup)};
    CHECK(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&child));
    ULONGLONG until=GetTickCount64()+4000;
    while(!receiver->window && GetTickCount64()<until) Sleep(10);CHECK(receiver->window);
    do {SetForegroundWindow((HWND)receiver->window);pump();} while(GetForegroundWindow()!=(HWND)receiver->window && GetTickCount64()<until);
    CHECK(GetForegroundWindow()==(HWND)receiver->window);
    PPFrame source=ring();
    if(argc==3){CHECK(!strcmp(argv[2],"--grab"));test_grab_region(&source);}
    else {test_alpha_and_rectangle(&source);test_capture_and_cleanup(&source);test_order_and_reuse(&source);test_canonical_layout();}
    pp_frame_destroy(&source);
    puts("PASS direct GL feasibility gate; macOS runtime unverified; timing and visible frame cadence require separate acceptance");
    return 0;
}
