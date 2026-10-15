#include "../../src/app/anchors.h"
#include "../../src/platform/windows/windows_internal.h"
#include "../../src/platform/windows/composition.h"
#include "pointy/content.h"
#include "pointy/text.h"
#include "receiver_shared.h"
#include "../test.h"
#include "../project_fixture.h"
#include <SDL3/SDL.h>
#include <math.h>
static PPAnchorPresenter *presenter;
static PPRenderer *renderer;
static PPAssets *assets;
static PPProject *project;
static PPDisplay *displays;
static size_t count;
static ReceiverState *receiver;
static HANDLE mapping;
static PROCESS_INFORMATION child;
static POINT old_cursor;
static HWND old_foreground;
static bool left_held;
static bool injected_ok=true;
static void trace_point(const char *stage,int x,int y);
static PPVec2 content_pointer(void) {return(PPVec2){displays[0].bounds.x+8,displays[0].bounds.y+8};}
static unsigned char *desktop_pixels(RECT rectangle) {
    bool pp_test_desktop_capture_start(RECT rectangle);unsigned char *pp_test_desktop_capture(RECT rectangle,bool require_new);
    static bool started;if(!started){CHECK(pp_test_desktop_capture_start(rectangle));started=true;}
    unsigned char *pixels=pp_test_desktop_capture(rectangle,false);CHECK(pixels);return pixels;
}
static PPAssetId import_file(const char *path,PPAssetKind kind) {
    size_t length=0;void *bytes=SDL_LoadFile(path,&length);CHECK(bytes&&length);
    PPAssetId id={0};PPError error={0};CHECK(pp_asset_import(assets,(PPBytes){bytes,length},kind,&id,&error));SDL_free(bytes);return id;
}
static void content_update(double now,PPVec2 pointer) {
    PPPlatformEvent event;PPError error={0};while(pp_platform_poll(&event)) CHECK(pp_anchor_presenter_event(presenter,&event,&error));
    CHECK(pp_anchor_presenter_update(presenter,displays,count,pointer,now,&error));
}
static void content_settle(double now) {
    ULONGLONG until=GetTickCount64()+250;
    do {content_update(now,content_pointer());Sleep(1);}while(GetTickCount64()<until);
    PPPresentationMetrics metrics=pp_anchor_presenter_metrics(presenter);CHECK(!metrics.worker_pending_jobs);
}
static int source_clamp(int value,int maximum) {return value<0?0:value>=maximum?maximum-1:value;}
static double source_channel(const PPFrame *frame,double x,double y,int channel) {
    x-=.5;y-=.5;int left=(int)floor(x),top=(int)floor(y);double fx=x-left,fy=y-top;
    int x0=source_clamp(left,frame->width),x1=source_clamp(left+1,frame->width);
    int y0=source_clamp(top,frame->height),y1=source_clamp(top+1,frame->height);
    double a=frame->pixels[(size_t)y0*frame->stride+x0*4+channel]*(1-fx)+frame->pixels[(size_t)y0*frame->stride+x1*4+channel]*fx;
    double b=frame->pixels[(size_t)y1*frame->stride+x0*4+channel]*(1-fx)+frame->pixels[(size_t)y1*frame->stride+x1*4+channel]*fx;
    return a*(1-fy)+b*fy;
}
/* Public source metadata supplies an independent bilinear premultiplied
 * reference. The actual image comes from Desktop Duplication, not GL export
 * or GDI, so stale/blank native publication cannot satisfy this comparison. */
static void content_pixels(const char *name,PPItemSnapshot *snapshot,double elapsed,bool matching) {
    PPError error={0};PPContentSources *sources=NULL;PPContentConsumer *consumer=NULL;
    CHECK(pp_content_sources_create(&sources,assets,&error));CHECK(pp_content_consumer_create(sources,&consumer,&error));
    PPVec2 layout=pp_item_snapshot_size(snapshot);double longest=fmax(layout.x,layout.y),density=displays[0].drawable_pixels_per_dip;
    PPContentFrame source={0};double next;ULONGLONG until=GetTickCount64()+250;
    do {pp_content_sources_poll(sources);CHECK(pp_content_frame(consumer,snapshot,longest,density,elapsed,&source,&next,&error));
        pp_content_accept(consumer);Sleep(1);
    }while(GetTickCount64()<until);
    if(source.source_rect.w<=0)source.source_rect=(PPRect){0,0,source.layout.x,source.layout.y};
    CHECK(source.frame&&source.source_rect.w>0&&source.source_rect.h>0);
    const PPAnchorReveal *reveal=pp_anchor_presenter_reveals(presenter)->entries;
    printf("ANCHOR REFERENCE source=%s revealed=%d opacity=%.17g preview=%.3f,%.3f,%.3f,%.3f target=%.3f,%.3f,%.3f,%.3f\n",name,reveal->revealed,reveal->opacity,reveal->preview.x,reveal->preview.y,reveal->preview.w,reveal->preview.h,reveal->target.x,reveal->target.y,reveal->target.w,reveal->target.h);
    CHECK(reveal->revealed&&reveal->opacity>0);
    PPRect bounds=reveal->preview;double cx=bounds.x+bounds.w*.5,cy=bounds.y+bounds.h*.5;
    RECT rectangle={(LONG)floor(bounds.x),(LONG)floor(bounds.y),(LONG)ceil(bounds.x+bounds.w),(LONG)ceil(bounds.y+bounds.h)};
    unsigned char *pixels=desktop_pixels(rectangle);
    double scale=longest*density/fmax(source.layout.x,source.layout.y),ratio=density/displays[0].desktop_units_per_dip;
    double angle=pp_item_snapshot_item(snapshot)->angle_deg*3.14159265358979323846/180,c=cos(angle),s=sin(angle);
    double opacity=pp_item_snapshot_item(snapshot)->opacity*reveal->opacity;unsigned visible=0,partial=0,samples=0,maximum=0;int worst_x=0,worst_y=0,worst_expected=0,worst_actual=0;
    int width=rectangle.right-rectangle.left;
    for(int y=rectangle.top+2;y<rectangle.bottom-2;y+=3)for(int x=rectangle.left+2;x<rectangle.right-2;x+=3) {
        /* The activation target has its own color and sits above the preview. */
        if(x<reveal->target.x+reveal->target.w+2&&y<reveal->target.y+reveal->target.h+2)continue;
        double dx=(x+.5-cx)*ratio,dy=(y+.5-cy)*ratio;
        double lx=(c*dx+s*dy)/scale+source.layout.x*.5,ly=(-s*dx+c*dy)/scale+source.layout.y*.5;
        double u=(lx-source.source_rect.x)/source.source_rect.w,v=(ly-source.source_rect.y)/source.source_rect.h;
        double rgba[4]={0};if(u>=0&&v>=0&&u<=1&&v<=1)for(int ch=0;ch<4;ch++)rgba[ch]=source_channel(source.frame,u*source.frame->width,v*source.frame->height,ch)*opacity;
        const unsigned char *actual=pixels+((size_t)(y-rectangle.top)*width+x-rectangle.left)*4;
        for(int ch=0;ch<3;ch++){int expected=(int)lround(rgba[ch]+255*(1-rgba[3]/255));unsigned delta=(unsigned)abs(actual[2-ch]-expected);if(delta>maximum){maximum=delta;worst_x=x;worst_y=y;worst_expected=expected;worst_actual=actual[2-ch];}}
        visible+=rgba[3]>1;partial+=rgba[3]>1&&rgba[3]<254;samples++;
    }
    free(pixels);printf("ANCHOR CONTENT source=%s elapsed=%.3f density=%.2f angle=%.2f samples=%u visible=%u partial=%u maximum_RGB_error=%u gate=4 expected_match=%d\n",name,elapsed,density,pp_item_snapshot_item(snapshot)->angle_deg,samples,visible,partial,maximum,matching);
    if(matching&&maximum>4){HWND window=WindowFromPoint((POINT){worst_x,worst_y});char klass[120]={0};GetClassNameA(window,klass,sizeof(klass));RECT native;GetWindowRect(window,&native);printf("ANCHOR mismatch x=%d y=%d expected=%d actual=%d top=%p class=%s bounds=%ld,%ld,%ld,%ld receiver=%p foreground=%p\n",worst_x,worst_y,worst_expected,worst_actual,(void *)window,klass,native.left,native.top,native.right,native.bottom,(void *)receiver->window,(void *)GetForegroundWindow());}
    CHECK(samples>50&&visible>10);CHECK(matching?maximum<=4:maximum>4);pp_content_consumer_destroy(consumer);pp_content_sources_destroy(sources);
}
static void test_shared_anchor_content(void) {
    pp_anchor_presenter_destroy(presenter);presenter=NULL;pp_displays_free(displays);displays=NULL;
    PPError error={0};CHECK(pp_platform_displays(&displays,&count,&error));CHECK(count>0);
    size_t primary=0;for(size_t i=0;i<count;i++)if(displays[i].bounds.x==0&&displays[i].bounds.y==0)primary=i;
    PPDisplay actual=displays[primary];pp_displays_free(displays);displays=malloc(sizeof(*displays));CHECK(displays);*displays=actual;count=1;
    CHECK(SetWindowLongPtrW((HWND)receiver->window,GWL_STYLE,WS_POPUP|WS_VISIBLE));
    CHECK(SetWindowPos((HWND)receiver->window,NULL,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),SWP_NOZORDER|SWP_FRAMECHANGED));
    CHECK(RedrawWindow((HWND)receiver->window,NULL,NULL,RDW_ERASE|RDW_INVALIDATE|RDW_UPDATENOW));
    /* An inset model display keeps the complete anchor and its reference on
     * the receiver's client area. Native presentation and capture stay real;
     * this is neither an OS display arrangement nor extra physical hardware. */
    displays[0].bounds=(PPRect){200,200,actual.bounds.w-400,actual.bounds.h-400};
    trace_point("anchor content receiver",250,250);
    HDC dc=GetDC(NULL);COLORREF gdi=GetPixel(dc,250,250);ReleaseDC(NULL,dc);
    printf("ANCHOR receiver GDI RGB=%u,%u,%u\n",GetRValue(gdi),GetGValue(gdi),GetBValue(gdi));
    Sleep(100);unsigned char *background=NULL;ULONGLONG deadline=GetTickCount64()+1000;
    do {free(background);background=desktop_pixels((RECT){250,250,251,251});if(background[0]==255&&background[1]==255&&background[2]==255)break;Sleep(20);}while(GetTickCount64()<deadline);
    printf("ANCHOR receiver background RGB=%u,%u,%u inset_model_display=200,200\n",background[2],background[1],background[0]);
    CHECK(background[0]==255&&background[1]==255&&background[2]==255);free(background);
    CHECK(pp_anchor_presenter_create(&presenter,renderer,project,&error));for(int a=0;a<8;a++)pp_anchor_presenter_enable(presenter,(PPAnchor)a,a==0);
    uint64_t id=pp_project_target(project,PP_ANCHOR_TOP_LEFT)->id;
    const char *names[]={"PNG","GIF","SVG","bitmap-Noto","scalable-Segoe","mixed-text"};
    PPAssetId noto=import_file(PP_RESOURCES "/fonts/NotoColorEmoji.ttf",PP_ASSET_FONT);
    PPItemSnapshot *stale=NULL;
    for(int kind=0;kind<6;kind++) {
        PPItem item={.id=id,.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=.63,.angle_deg=17.25};
        if(kind==0)item.image_asset=import_file(PP_MEDIA_FIXTURES "/retained-blue.png",PP_ASSET_PNG);
        if(kind==1)item.image_asset=import_file(PP_MEDIA_FIXTURES "/finite.gif",PP_ASSET_GIF);
        if(kind==2){const char *svg="<svg width='96' height='64'><rect x='4' y='4' width='88' height='56' rx='12' fill='#ef6f21'/><path d='M22 32H74M56 16L74 32L56 48' fill='none' stroke='white' stroke-width='5'/></svg>";CHECK(pp_asset_import(assets,(PPBytes){(const uint8_t *)svg,strlen(svg)},PP_ASSET_SVG,&item.image_asset,&error));}
        if(kind>=3){item.kind=PP_ITEM_TEXT;item.text=kind==5?"A\xf0\x9f\x91\x89\nB":"\xf0\x9f\x91\x89";CHECK(pp_text_default_style(assets,&item.text_style,&error));
            if(kind==3)item.text_style=(PPTextStyle){.requested_family="Noto Color Emoji",.fallback_asset=noto,.font_size_dip=96,.color={0,0,0,255}};}
        CHECK(pp_project_edit_item(project,id,&item,&error));double start=20+kind;content_update(start,content_pointer());content_settle(start+.25);
        PPItemSnapshot *snapshot=pp_project_snapshot(project,id);CHECK(snapshot);content_pixels(names[kind],snapshot,.25,true);
        if(kind==0)stale=pp_item_snapshot_retain(snapshot);
        if(kind==1){content_pixels("stale-PNG-control",stale,.25,false);pp_item_snapshot_release(stale);stale=NULL;}
        PPRendererMetrics before=pp_anchor_presenter_renderer_metrics(presenter);uint64_t builds=pp_assets_source_metrics(assets).raster_builds;
        content_settle(start+.25);PPRendererMetrics after=pp_anchor_presenter_renderer_metrics(presenter);
        CHECK(after.source_uploads==before.source_uploads&&after.transform_draws==before.transform_draws&&after.readbacks==before.readbacks);
        CHECK(pp_assets_source_metrics(assets).raster_builds==builds);
        pp_anchor_presenter_enable(presenter,PP_ANCHOR_TOP_LEFT,false);content_update(start+.3,(PPVec2){600,600});content_update(start+.6,(PPVec2){600,600});
        CHECK(pp_anchor_presenter_gpu_resources(presenter).retained_bytes==0);
        pp_anchor_presenter_enable(presenter,PP_ANCHOR_TOP_LEFT,true);content_update(start+.7,content_pointer());content_settle(start+.95);
        CHECK(pp_assets_source_metrics(assets).raster_builds==builds);content_pixels(names[kind],snapshot,.25,true);pp_item_snapshot_release(snapshot);
    }
    pp_anchor_presenter_destroy(presenter);presenter=NULL;pp_project_destroy(project);project=NULL;CHECK(!pp_assets_external_raster_bytes(assets));
    CHECK(GetForegroundWindow()==(HWND)receiver->window);
    puts("PASS native anchor PNG/GIF/SVG/bitmap/scalable/mixed replacements match independent premultiplied desktop pixels; unchanged and hide/show source reuse; hidden output and final raster charges released exactly");
}
static void trace_point(const char *stage,int x,int y) {
    HWND top=WindowFromPoint((POINT){x,y});char class_name[100]={0};GetClassNameA(top,class_name,sizeof(class_name));
    RECT window,client;GetWindowRect((HWND)receiver->window,&window);GetClientRect((HWND)receiver->window,&client);POINT origin={0};ClientToScreen((HWND)receiver->window,&origin);
    printf("%s: point=(%d,%d) top=%p class=%s receiver=%p foreground=%p receiver bounds=(%ld,%ld,%ld,%ld) client origin=(%ld,%ld) size=(%ld,%ld)\n",stage,x,y,(void *)top,class_name,(void *)receiver->window,(void *)GetForegroundWindow(),window.left,window.top,window.right,window.bottom,origin.x,origin.y,client.right,client.bottom);
    if(!strcmp(class_name,"PointyPointer.Surface")) {
        PPSurface *surface=(PPSurface *)GetWindowLongPtrW(top,GWLP_USERDATA);int local_x=x-surface->origin.x,local_y=y-surface->origin.y;
        int alpha=local_x>=0&&local_y>=0&&local_x<surface->width&&local_y<surface->height?surface->bits[((size_t)local_y*surface->width+local_x)*4+3]:-1;
        printf("Native overlay origin=(%ld,%ld) size=(%d,%d) alpha=%d\n",surface->origin.x,surface->origin.y,surface->width,surface->height,alpha);
    }
}
static void cleanup(void) {
    pp_anchor_presenter_destroy(presenter);pp_renderer_destroy(renderer);pp_displays_free(displays);pp_platform_release_capture();
    pp_project_destroy(project);pp_assets_destroy(assets);
    if(left_held) {INPUT input={0};input.type=INPUT_MOUSE;input.mi.dwFlags=MOUSEEVENTF_LEFTUP;SendInput(1,&input,sizeof(input));}
    if(receiver&&receiver->window) PostMessageW((HWND)receiver->window,WM_CLOSE,0,0);
    if(child.hProcess) {WaitForSingleObject(child.hProcess,3000);CloseHandle(child.hProcess);CloseHandle(child.hThread);}
    if(receiver) UnmapViewOfFile(receiver);if(mapping) CloseHandle(mapping);
    SetCursorPos(old_cursor.x,old_cursor.y);if(old_foreground) SetForegroundWindow(old_foreground);
}
static void pump(unsigned ms) {
    PPError error={0};ULONGLONG end=GetTickCount64()+ms;
    do {PPPlatformEvent event;while(pp_platform_poll(&event)) CHECK(pp_anchor_presenter_event(presenter,&event,&error));
        PPVec2 pointer;CHECK(pp_platform_pointer(&pointer));
        CHECK(pp_anchor_presenter_update(presenter,displays,count,pointer,GetTickCount64()/1000.0,&error));Sleep(1);
    }while(GetTickCount64()<end);
}
static DWORD WINAPI inject(void *value) {INPUT *input=value;injected_ok=SendInput(1,input,sizeof(*input))==1;return 0;}
static void inject_input(INPUT input) {
    HANDLE thread=CreateThread(NULL,0,inject,&input,0,NULL);CHECK(thread);
    ULONGLONG end=GetTickCount64()+3000;
    while(WaitForSingleObject(thread,0)==WAIT_TIMEOUT&&GetTickCount64()<end) pump(1);
    CHECK(WaitForSingleObject(thread,0)==WAIT_OBJECT_0);CloseHandle(thread);CHECK(injected_ok);pump(35);
}
static void move(int x,int y) {
    INPUT input={0};input.type=INPUT_MOUSE;input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
    input.mi.dx=MulDiv(x-GetSystemMetrics(SM_XVIRTUALSCREEN),65535,GetSystemMetrics(SM_CXVIRTUALSCREEN)-1);
    input.mi.dy=MulDiv(y-GetSystemMetrics(SM_YVIRTUALSCREEN),65535,GetSystemMetrics(SM_CYVIRTUALSCREEN)-1);inject_input(input);
    POINT actual;CHECK(GetCursorPos(&actual));if(abs(actual.x-x)>1||abs(actual.y-y)>1) {fprintf(stderr,"External cursor interference: expected (%d,%d), observed (%ld,%ld)\n",x,y,actual.x,actual.y);exit(1);}
}
static void mouse(DWORD flags) {if(flags&MOUSEEVENTF_LEFTDOWN) left_held=true;INPUT input={0};input.type=INPUT_MOUSE;input.mi.dwFlags=flags;inject_input(input);if(flags&MOUSEEVENTF_LEFTUP) left_held=false;}
static void click(int x,int y) {move(x,y);mouse(MOUSEEVENTF_LEFTDOWN);mouse(MOUSEEVENTF_LEFTUP);}
static BOOL CALLBACK find_sprite(HWND window,LPARAM context) {
    DWORD process;GetWindowThreadProcessId(window,&process);if(process!=GetCurrentProcessId()) return TRUE;
    PPSurface *s=(PPSurface *)GetPropW(window,L"PointyPointer.DirectSurface");
    if(!s){char name[80]={0};GetClassNameA(window,name,sizeof(name));if(strcmp(name,"PointyPointer.Surface")) return TRUE;
      s=(PPSurface *)GetWindowLongPtrW(window,GWLP_USERDATA);}
    if(s&&s->instance_id==1) {*(PPSurface **)context=s;return FALSE;}return TRUE;
}
static void check_seam_sprite(double pointer_x,double grab_x,int expected_width) {
    PPPose pose;CHECK(pp_anchor_presenter_instance_pose(presenter,0,&pose));PPSurface *s=NULL;EnumWindows(find_sprite,(LPARAM)&s);
    CHECK(s&&IsWindowVisible(s->window));CHECK(s->width==expected_width+1);CHECK(pose.longest_dip==96);
    printf("ANCHOR PRESENTATION %s\n",pp_composition_description(s->composition));
    CHECK(fabs(pose.center.x+grab_x*expected_width/96-pointer_x)<2);
    CHECK(fabs(s->origin.x+s->width*.5+grab_x*expected_width/96-pointer_x)<2);
}
static void test_native_presentation_at_simulated_mixed_scale_seam(void) {
    pp_anchor_presenter_destroy(presenter);presenter=NULL;pp_displays_free(displays);count=2;displays=calloc(count,sizeof(*displays));CHECK(displays);
    /* Two model displays share a physical test coordinate x=400 on the single
     * actual monitor. The native surfaces/input are real; this is not physical
     * two-monitor coverage. Both pointer and center are inside its bounds. */
    displays[0]=(PPDisplay){101,{0,0,400,1080},1.5,1.5};displays[1]=(PPDisplay){102,{400,0,1000,1080},1,1};
    PPError error={0};CHECK(pp_anchor_presenter_create(&presenter,renderer,project,&error));
    for(int a=0;a<8;a++) pp_anchor_presenter_enable(presenter,(PPAnchor)a,a==PP_ANCHOR_TOP_RIGHT);
    move(390,8);move(380,72);mouse(MOUSEEVENTF_LEFTDOWN);move(410,200);
    double grab=(380-328)/1.5;check_seam_sprite(410,grab,96);
    PPPose pose;CHECK(pp_anchor_presenter_instance_pose(presenter,0,&pose));CHECK(pose.center.x<400); /* Center left, pointer right. */
    mouse(MOUSEEVENTF_LEFTUP);check_seam_sprite(410,grab,96);
    mouse(MOUSEEVENTF_LEFTDOWN);move(390,200);check_seam_sprite(390,grab,144);
    move(410,200);check_seam_sprite(410,grab,96);mouse(MOUSEEVENTF_LEFTUP);check_seam_sprite(410,grab,96);
    CHECK(GetForegroundWindow()==(HWND)receiver->window);
    puts("PASS real native presentation for anchor/placed drags at simulated 1.5/1 mixed-DPI seam, including release geometry");
}
static void test_native_anchors(void) {
    PPError error={0};CHECK(pp_platform_displays(&displays,&count,&error));CHECK(count>0);
    printf("Hardware enumeration: %zu physical monitor(s); second-display hardware coverage requires >=2.\n",count);
    /* Use the actual primary monitor origin; relocate the independent receiver
     * under the edge target without altering the user's display arrangement. */
    size_t primary=0;for(size_t i=0;i<count;i++) if(displays[i].bounds.x==0&&displays[i].bounds.y==0) primary=i;
    PPDisplay actual=displays[primary];free(displays);displays=malloc(sizeof(*displays));CHECK(displays);displays[0]=actual;count=1;
    printf("Primary bounds=(%.0f,%.0f,%.0f,%.0f), desktop units/DIP=%.3f, drawable pixels/DIP=%.3f\n",actual.bounds.x,actual.bounds.y,actual.bounds.w,actual.bounds.h,actual.desktop_units_per_dip,actual.drawable_pixels_per_dip);
    CHECK(SetWindowPos((HWND)receiver->window,NULL,(int)actual.bounds.x-8,(int)actual.bounds.y-32,380,330,SWP_NOZORDER));
    CHECK(pp_assets_create(&assets,&error));project=test_arrow_project(assets);
    CHECK(pp_renderer_create(&renderer,&error));CHECK(pp_anchor_presenter_create(&presenter,renderer,project,&error));
    for(int a=1;a<8;a++) pp_anchor_presenter_enable(presenter,(PPAnchor)a,false);
    int transparent_x=(int)round(25*actual.desktop_units_per_dip),transparent_y=(int)round(80*actual.desktop_units_per_dip);
    move(250,220);pump(150);LONG clicks=receiver->clicks;trace_point("before initial click",transparent_x,transparent_y);click(transparent_x,transparent_y);trace_point("after initial click",transparent_x,transparent_y);CHECK(receiver->clicks==clicks+1);
    move(8,8);const PPAnchorRuntime *r=pp_anchor_presenter_reveals(presenter);CHECK(r->entries[0].revealed);
    /* Target-to-visible-preview transition, then grab the visible arrow shaft. */
    int px=(int)round(50*actual.desktop_units_per_dip),py=(int)round(48*actual.desktop_units_per_dip);
    move(px,py);CHECK(r->entries[0].revealed&&!r->entries[0].leaving);pump(150);CHECK(r->entries[0].revealed);
    /* Same monitor ID and scale, new origin/resolution. Keep reveal opacity
     * constant so a fade cannot accidentally invalidate stale native geometry. */
    PPError geometry_error={0};displays[0].bounds.x=200;displays[0].bounds.w=3000;
    CHECK(pp_anchor_presenter_update(presenter,displays,count,(PPVec2){208,8},GetTickCount64()/1000.0,&geometry_error));
    CHECK(WindowFromPoint((POINT){px,py})==(HWND)receiver->window);
    HWND shifted=WindowFromPoint((POINT){200+px,py});CHECK(GetPropW(shifted,L"PointyPointer.DirectSurface"));
    displays[0]=actual;CHECK(pp_anchor_presenter_update(presenter,displays,count,(PPVec2){8,8},GetTickCount64()/1000.0,&geometry_error));
    clicks=receiver->clicks;click(transparent_x,transparent_y);CHECK(receiver->clicks==clicks+1);CHECK(pp_anchor_presenter_instance_count(presenter)==0);CHECK(!pp_windows_input_owner());
    click(8,8);CHECK(pp_anchor_presenter_instance_count(presenter)==0); /* Below system threshold. */
    move(px,py);mouse(MOUSEEVENTF_LEFTDOWN);move(200,150);CHECK(pp_anchor_presenter_instance_count(presenter)==1);
    PPPose before;CHECK(pp_anchor_presenter_instance_pose(presenter,0,&before));CHECK(before.longest_dip==96);
    /* Simulate a scale event during real native capture. No OS settings change. */
    double changed_scale=actual.desktop_units_per_dip==1?1.5:1;
    CHECK(changed_scale!=actual.desktop_units_per_dip);
    printf("Simulated native-drag scale event: %.3f -> %.3f\n",actual.desktop_units_per_dip,changed_scale);
    displays[0].desktop_units_per_dip=displays[0].drawable_pixels_per_dip=changed_scale;pump(20);move(230,170);
    PPPose after;CHECK(pp_anchor_presenter_instance_pose(presenter,0,&after));CHECK(after.longest_dip==96);
    double grab_x=px/actual.desktop_units_per_dip-48,grab_y=py/actual.desktop_units_per_dip-48;
    CHECK(fabs(after.center.x-(230-grab_x*changed_scale))<2);CHECK(fabs(after.center.y-(170-grab_y*changed_scale))<2);
    /* Removing the source display cancels capture and recovers the sprite onto
     * a synthetic remaining display. Geometry is simulated; OS routing is real. */
    displays[0]=(PPDisplay){actual.id+1,{400,100,400,400},1,1};pump(20);
    CHECK(pp_anchor_presenter_instance_pose(presenter,0,&after));CHECK(after.center.x>=448&&after.center.x<=752&&after.center.y>=148&&after.center.y<=452);
    CHECK(pp_windows_input_owner()==NULL);mouse(MOUSEEVENTF_LEFTUP);CHECK(GetForegroundWindow()==(HWND)receiver->window);
    puts("PASS native outside-target first click and transparent-preview pass-through, visible preview drag, target/preview transition, threshold cancellation, simulated scale/disconnect during native drag.");
    test_native_presentation_at_simulated_mixed_scale_seam();
}
static int diagnose_without_input(void) {
    PPError error={0};PPDisplay *items=NULL;size_t n=0;CHECK(pp_platform_displays(&items,&n,&error));
    for(size_t i=0;i<n;i++) printf("Display %llu: bounds=(%.0f,%.0f,%.0f,%.0f) desktop units/DIP=%.3f drawable pixels/DIP=%.3f\n",(unsigned long long)items[i].id,items[i].bounds.x,items[i].bounds.y,items[i].bounds.w,items[i].bounds.h,items[i].desktop_units_per_dip,items[i].drawable_pixels_per_dip);
    /* Same frame style and DefWindowProc geometry as the independent receiver,
     * shown without activation. No cursor or focus restoration is necessary:
     * this mode never changes either, including while the user is moving. */
    WNDCLASSW type={0};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(NULL);type.lpszClassName=L"PointyPointer.ReceiverGeometryDiagnostic";CHECK(RegisterClassW(&type));
    HWND window=CreateWindowExW(WS_EX_NOACTIVATE,type.lpszClassName,L"PointyPointer geometry diagnostic",WS_OVERLAPPEDWINDOW,160,160,360,320,NULL,NULL,type.hInstance,NULL);CHECK(window);
    ReceiverState temporary={0};temporary.window=(ULONG_PTR)window;receiver=&temporary;
    CHECK(SetWindowPos(window,HWND_TOP,-8,-32,380,330,SWP_NOACTIVATE|SWP_SHOWWINDOW));
    trace_point("receiver geometry without input",25,80);
    DestroyWindow(window);receiver=NULL;pp_displays_free(items);return 0;
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--diagnose")) {SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);return diagnose_without_input();}
    CHECK(argc==2);GetCursorPos(&old_cursor);old_foreground=GetForegroundWindow();atexit(cleanup);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    char name[80];snprintf(name,sizeof(name),"Local\\PointyAnchors-%lu",GetCurrentProcessId());
    mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(ReceiverState),name);CHECK(mapping);
    receiver=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(*receiver));CHECK(receiver);
    char command[2048];snprintf(command,sizeof(command),"\"%s\" %s",argv[1],name);STARTUPINFOA startup={sizeof(startup)};
    CHECK(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&child));
    ULONGLONG end=GetTickCount64()+5000;while(!receiver->window&&GetTickCount64()<end) Sleep(10);
    CHECK(receiver->window);CHECK(SetForegroundWindow((HWND)receiver->window));test_native_anchors();test_shared_anchor_content();return 0;
}
