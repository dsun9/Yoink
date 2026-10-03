#include "../../src/app/anchors.h"
#include "../../src/platform/windows/windows_internal.h"
#include "receiver_shared.h"
#include "../test.h"
#include "../project_fixture.h"
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
    char name[80]={0};GetClassNameA(window,name,sizeof(name));if(strcmp(name,"PointyPointer.Surface")) return TRUE;
    PPSurface *s=(PPSurface *)GetWindowLongPtrW(window,GWLP_USERDATA);
    if(s&&s->instance_id==1) {*(PPSurface **)context=s;return FALSE;}return TRUE;
}
static void check_seam_sprite(double pointer_x,double grab_x,int expected_width) {
    PPPose pose;CHECK(pp_anchor_presenter_instance_pose(presenter,0,&pose));PPSurface *s=NULL;EnumWindows(find_sprite,(LPARAM)&s);
    CHECK(s&&IsWindowVisible(s->window));CHECK(s->width==expected_width);CHECK(pose.longest_dip==96);
    CHECK(fabs(pose.center.x+grab_x*s->width/96-pointer_x)<2);
    CHECK(fabs(s->origin.x+s->width*.5+grab_x*s->width/96-pointer_x)<2);
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
    /* Target-to-visible-preview transition and a transparent preview pixel. */
    int px=(int)round(50*actual.desktop_units_per_dip),py=(int)round(48*actual.desktop_units_per_dip);
    move(px,py);CHECK(r->entries[0].revealed&&!r->entries[0].leaving);pump(150);CHECK(r->entries[0].revealed);
    /* Same monitor ID and scale, new origin/resolution. Keep reveal opacity
     * constant so a fade cannot accidentally invalidate stale native geometry. */
    PPError geometry_error={0};displays[0].bounds.x=200;displays[0].bounds.w=3000;
    CHECK(pp_anchor_presenter_update(presenter,displays,count,(PPVec2){208,8},GetTickCount64()/1000.0,&geometry_error));
    CHECK(WindowFromPoint((POINT){px,py})==(HWND)receiver->window);
    HWND shifted=WindowFromPoint((POINT){200+px,py});char shifted_class[80]={0};GetClassNameA(shifted,shifted_class,sizeof(shifted_class));CHECK(!strcmp(shifted_class,"PointyPointer.Surface"));
    displays[0]=actual;CHECK(pp_anchor_presenter_update(presenter,displays,count,(PPVec2){8,8},GetTickCount64()/1000.0,&geometry_error));
    clicks=receiver->clicks;click(transparent_x,transparent_y);CHECK(receiver->clicks==clicks+1);CHECK(pp_anchor_presenter_instance_count(presenter)==0);
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
    puts("PASS native outside-target and transparent-preview first clicks, target/preview transition, threshold cancellation, simulated scale/disconnect during native drag.");
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
    CHECK(receiver->window);CHECK(SetForegroundWindow((HWND)receiver->window));test_native_anchors();return 0;
}
