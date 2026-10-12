#include "pointy/assets.h"
#include "pointy/platform.h"
#include "../../src/app/presenter_internal.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
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
static PPProject *project;
static PPAnchorPresenter *presenter;
static unsigned direct_downs[4],direct_ups[4];
static void cleanup(void) {
    pp_anchor_presenter_destroy(presenter);pp_project_destroy(project);
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
        if(presenter) {
            if(e.instance_id<4 && e.kind==PP_EVENT_BUTTON_DOWN)direct_downs[e.instance_id]++;
            if(e.instance_id<4 && e.kind==PP_EVENT_BUTTON_UP)direct_ups[e.instance_id]++;
            PPError error={0};CHECK(pp_anchor_presenter_event(presenter,&e,&error));
        }
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
static PPItem image(const char *name,PPAssetKind kind) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s",PP_MEDIA_FIXTURES,name);
    size_t count;void *bytes=SDL_LoadFile(path,&count);CHECK(bytes);PPError error={0};
    PPItem item={.kind=PP_ITEM_IMAGE,.image_longest_dip=64,.opacity=1};
    CHECK(pp_asset_import(assets,(PPBytes){bytes,count},kind,&item.image_asset,&error));SDL_free(bytes);return item;
}
static HWND native(uint64_t id) {
    PPSprite *sprite=pp_sprites_find(presenter,id);CHECK(sprite);
    SDL_Window *window=SDL_GetWindowFromID(pp_surface_window_id(sprite->surface));CHECK(window);
    return SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,NULL);
}
static void pixel(const char *label,int x,int y,int r,int g,int b) {
    ULONGLONG until=GetTickCount64()+1000;COLORREF value=CLR_INVALID;
    do {pump();HDC dc=GetDC(NULL);CHECK(dc);value=GetPixel(dc,x,y);ReleaseDC(NULL,dc);
        if(value!=CLR_INVALID && abs((int)GetRValue(value)-r)<=3 && abs((int)GetGValue(value)-g)<=3 && abs((int)GetBValue(value)-b)<=3)break;
    }while(GetTickCount64()<until);
    printf("GIF compositor %s RGB=%d,%d,%d expected=%d,%d,%d\n",label,GetRValue(value),GetGValue(value),GetBValue(value),r,g,b);
    CHECK(value!=CLR_INVALID && abs((int)GetRValue(value)-r)<=3 && abs((int)GetGValue(value)-g)<=3 && abs((int)GetBValue(value)-b)<=3);
}
static void direct_update(double now) {PPError error={0};pp_sprites_sync(presenter);CHECK(pp_sprites_update(presenter,now,&error));pump();}
static void direct_mouse(int x,int y,DWORD flags) {
    CHECK(SetCursorPos(x,y));if(flags&MOUSEEVENTF_LEFTDOWN)left_held=true;
    INPUT input={.type=INPUT_MOUSE};input.mi.dwFlags=flags;send_input(input);if(flags&MOUSEEVENTF_LEFTUP)left_held=false;
    CHECK(GetForegroundWindow()==(HWND)receiver->window);
}
static void retained_native(void) {
    PPError error={0};pp_surface_destroy(surface);surface=NULL;
    CHECK(pp_project_create(assets,&project,&error));PPItem finite=image("finite.gif",PP_ASSET_GIF);
    CHECK(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&finite,NULL,&error));
    CHECK(pp_anchor_presenter_create(&presenter,renderer,project,&error));
    PPDisplay *displays=NULL;size_t count;CHECK(pp_platform_displays(&displays,&count,&error));
    const PPDisplay *selected=pp_display_at(displays,count,(PPVec2){300,300});CHECK(selected);
    presenter->displays=malloc(sizeof(*selected));*presenter->displays=*selected;presenter->display_count=1;pp_displays_free(displays);
    uint64_t ids[3];for(int i=0;i<3;i++) {
        PPSprite *sprite=pp_sprites_create(presenter,PP_ANCHOR_TOP_LEFT,(PPPose){{240+i*110,300},64,0,1},i?1.06:1,&error);CHECK(sprite);ids[i]=sprite->id;
    }
    PPRendererMetrics before=pp_anchor_presenter_renderer_metrics(presenter);uint64_t builds=pp_assets_source_metrics(assets).raster_builds;
    direct_update(1.08);PPRendererMetrics first=pp_anchor_presenter_renderer_metrics(presenter);
    CHECK(first.source_uploads==before.source_uploads+2 && first.transform_draws==before.transform_draws+3 && first.readbacks==before.readbacks);
    CHECK(pp_anchor_presenter_gpu_resources(presenter).retained_bytes==24);
    printf("GIF GL renderer=%s vendor=%s version=%s shared_frames=2 independent_instances=3 native_density=%.2f\n",glGetString(GL_RENDERER),glGetString(GL_VENDOR),glGetString(GL_VERSION),presenter->displays[0].drawable_pixels_per_dip);
    RECT a,b;CHECK(GetWindowRect(native(ids[0]),&a));CHECK(GetWindowRect(native(ids[1]),&b));
    pixel("earlier clock green",240,300,0,255,0);pixel("later clock red",b.left+5,300,255,0,0);pixel("visible frame transparent interior",350,300,255,255,255);
    unsigned down=direct_downs[ids[1]];LONG below=receiver->clicks;
    direct_mouse(350,300,MOUSEEVENTF_LEFTDOWN);direct_mouse(350,300,MOUSEEVENTF_LEFTUP);
    CHECK(direct_downs[ids[1]]==down+1 && receiver->clicks==below);
    direct_mouse(350,b.bottom+2,MOUSEEVENTF_LEFTDOWN);direct_mouse(350,b.bottom+2,MOUSEEVENTF_LEFTUP);CHECK(receiver->clicks==below+1);
    PPSprite *sprite=pp_sprites_find(presenter,ids[1]);sprite->pose.center.y+=10;sprite->dirty=true;direct_update(1.081);
    PPRendererMetrics moved=pp_anchor_presenter_renderer_metrics(presenter);
    CHECK(moved.source_uploads==first.source_uploads && moved.transform_draws==first.transform_draws && pp_assets_source_metrics(assets).raster_builds==builds);
    pp_scene_delete(presenter->scene,ids[2]);direct_update(1.082);CHECK(pp_anchor_presenter_gpu_resources(presenter).retained_bytes==24);
    pixel("remaining shared consumer",b.left+5,310,255,0,0);
    PPItem changing=image("changing-alpha.gif",PP_ASSET_GIF);CHECK(pp_scene_replace(presenter->scene,ids[1],&changing,1.08,&error));direct_update(1.08);
    direct_mouse(350,310,MOUSEEVENTF_LEFTDOWN);direct_update(1.19);CHECK(!IsWindowVisible(native(ids[1])));
    direct_mouse(350,310,MOUSEEVENTF_LEFTUP);below=receiver->clicks;direct_mouse(350,310,MOUSEEVENTF_LEFTDOWN);direct_mouse(350,310,MOUSEEVENTF_LEFTUP);CHECK(receiver->clicks==below+1);
    direct_update(1.29);CHECK(IsWindowVisible(native(ids[1])));pixel("stationary transparent frame returns",350,310,255,0,0);
    down=direct_downs[ids[1]];direct_mouse(350,310,MOUSEEVENTF_LEFTDOWN);direct_mouse(350,310,MOUSEEVENTF_LEFTUP);CHECK(direct_downs[ids[1]]==down+1);
    PPItem png=image("retained-blue.png",PP_ASSET_PNG);CHECK(pp_scene_replace(presenter->scene,ids[1],&png,1.3,&error));direct_update(1.3);pixel("GIF to owned PNG",350,310,0,0,255);
    CHECK(pp_scene_replace(presenter->scene,ids[1],&finite,1.31,&error));direct_update(1.31);CHECK(GetWindowRect(native(ids[1]),&b));pixel("owned PNG to shared GIF",b.left+5,310,255,0,0);
    direct_update(2);PPRendererMetrics stopped=pp_anchor_presenter_renderer_metrics(presenter);direct_update(3);
    CHECK(pp_anchor_presenter_renderer_metrics(presenter).transform_draws==stopped.transform_draws);
    pp_scene_clear(presenter->scene);direct_update(4);CHECK(pp_anchor_presenter_gpu_resources(presenter).retained_bytes==0);
    CHECK(pp_anchor_presenter_renderer_metrics(presenter).readbacks==before.readbacks);
    puts("PASS retained native GIF independent clocks/shared immutable pixels, rectangle first clicks, stationary alpha/capture, nonactivation, ownership transitions, finite stop and zero-byte cleanup");
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
    puts("PASS actual GIF: stationary pixel becomes transparent; next click and wheel reach receiver; focus preserved.");
    retained_native();return 0;
}
