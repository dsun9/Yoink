#include "pointy/assets.h"
#include "pointy/platform.h"
#include "../platform/receiver_shared.h"
#include "../test.h"
static PPAssets *assets;
static PPAsset *gif;
static PPSurface *surface;
static PPRenderer *renderer;
static PPFrame presented;
static PROCESS_INFORMATION child;
static ReceiverState *receiver;
static HANDLE mapping;
static HWND old_foreground;
static POINT old_cursor;
static bool left_held;
static int clicks,wheels;
static void cleanup(void) {
    pp_surface_destroy(surface);pp_renderer_destroy(renderer);pp_platform_release_capture();
    if(left_held) { INPUT up={0};up.type=INPUT_MOUSE;up.mi.dwFlags=MOUSEEVENTF_LEFTUP;SendInput(1,&up,sizeof(up)); }
    pp_frame_destroy(&presented);pp_asset_release(gif);pp_assets_destroy(assets);
    if(receiver && receiver->window) PostMessageW((HWND)receiver->window,WM_CLOSE,0,0);
    if(child.hProcess) { WaitForSingleObject(child.hProcess,3000);CloseHandle(child.hThread);CloseHandle(child.hProcess); }
    if(receiver) UnmapViewOfFile(receiver);if(mapping) CloseHandle(mapping);
    SetCursorPos(old_cursor.x,old_cursor.y);if(old_foreground) SetForegroundWindow(old_foreground);
}
static void pump(void) {
    ULONGLONG until=GetTickCount64()+70;
    do { PPPlatformEvent e;while(pp_platform_poll(&e)) {
        if(e.kind==PP_EVENT_BUTTON_DOWN && e.surface==surface) clicks++;
        if(e.kind==PP_EVENT_WHEEL && e.surface==surface) wheels++;
    } Sleep(1); } while(GetTickCount64()<until);
}
typedef struct Injection { INPUT input;UINT sent; } Injection;
static DWORD WINAPI inject(void *p) { Injection *i=p;i->sent=SendInput(1,&i->input,sizeof(INPUT));return 0; }
static void send_input(INPUT input) {
    Injection i={input,0};HANDLE thread=CreateThread(NULL,0,inject,&i,0,NULL);CHECK(thread);
    ULONGLONG until=GetTickCount64()+5000;
    while(WaitForSingleObject(thread,0)==WAIT_TIMEOUT && GetTickCount64()<until) pump();
    CHECK(WaitForSingleObject(thread,0)==WAIT_OBJECT_0);CloseHandle(thread);CHECK(i.sent==1);pump();
}
static void stationary(void) {
    POINT p;CHECK(GetCursorPos(&p));CHECK(p.x==252 && p.y==230);
    CHECK(GetForegroundWindow()==(HWND)receiver->window);
}
static void mouse(DWORD flags,DWORD data) {
    stationary();if(flags&MOUSEEVENTF_LEFTDOWN) left_held=true;
    INPUT i={0};i.type=INPUT_MOUSE;i.mi.dwFlags=flags;i.mi.mouseData=data;send_input(i);
    if(flags&MOUSEEVENTF_LEFTUP) left_held=false;stationary();
}
static void present(double elapsed) {
    PPError e={0};const PPFrame *f;double next;CHECK(pp_asset_frame(gif,elapsed,0,0,&f,&next,&e));
    pp_frame_destroy(&presented);CHECK(pp_renderer_transform(renderer,f,64,0,1,1,&presented,&e));
    CHECK(pp_surface_present(surface,&presented,(PPRect){220,220,presented.width,presented.height},&e));pump();
}
int main(int argc,char **argv) {
    CHECK(argc==2);old_foreground=GetForegroundWindow();CHECK(GetCursorPos(&old_cursor));atexit(cleanup);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    char name[80];snprintf(name,sizeof(name),"Local\\PointyGifReceiver-%lu",GetCurrentProcessId());
    mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(ReceiverState),name);CHECK(mapping);
    receiver=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(*receiver));CHECK(receiver);
    char command[2048];snprintf(command,sizeof(command),"\"%s\" %s",argv[1],name);STARTUPINFOA startup={sizeof(startup)};
    CHECK(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&child));
    ULONGLONG until=GetTickCount64()+5000;while(!receiver->window && GetTickCount64()<until) Sleep(10);
    CHECK(receiver->window);CHECK(SetForegroundWindow((HWND)receiver->window));
    FILE *file=fopen(PP_MEDIA_FIXTURES "/changing-alpha.gif","rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));long n=ftell(file);CHECK(n>0);rewind(file);
    uint8_t *bytes=malloc((size_t)n);CHECK(bytes);CHECK(fread(bytes,1,(size_t)n,file)==(size_t)n);fclose(file);
    PPError e={0};PPAssetId id;CHECK(pp_assets_create(&assets,&e));CHECK(pp_asset_import(assets,(PPBytes){bytes,(size_t)n},PP_ASSET_GIF,&id,&e));free(bytes);
    gif=pp_asset_retain(assets,id);CHECK(gif);CHECK(pp_renderer_create(&renderer,&e));CHECK(pp_surface_create(&surface,&e));pp_surface_set_instance_id(surface,44);
    present(0);
    INPUT move={0};move.type=INPUT_MOUSE;move.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
    move.mi.dx=MulDiv(252-GetSystemMetrics(SM_XVIRTUALSCREEN),65535,GetSystemMetrics(SM_CXVIRTUALSCREEN)-1);
    move.mi.dy=MulDiv(230-GetSystemMetrics(SM_YVIRTUALSCREEN),65535,GetSystemMetrics(SM_CYVIRTUALSCREEN)-1);send_input(move);stationary();
    mouse(MOUSEEVENTF_LEFTDOWN,0);mouse(MOUSEEVENTF_LEFTUP,0);CHECK(clicks==1 && receiver->clicks==0);
    mouse(MOUSEEVENTF_WHEEL,WHEEL_DELTA);CHECK(wheels==1 && receiver->wheels==0);
    /* No movement between the actual GIF frame change and the next input. */
    present(.11);stationary();CHECK(presented.content_alpha[10*presented.width+32]==0);
    mouse(MOUSEEVENTF_LEFTDOWN,0);mouse(MOUSEEVENTF_LEFTUP,0);CHECK(clicks==1 && receiver->clicks==1);
    mouse(MOUSEEVENTF_WHEEL,WHEEL_DELTA);CHECK(wheels==1 && receiver->wheels==1);
    puts("PASS actual GIF: stationary pixel becomes transparent; next click and wheel reach receiver; focus preserved.");return 0;
}
