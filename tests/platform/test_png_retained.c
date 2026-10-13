#include "../../src/app/presenter_internal.h"
#include "receiver_shared.h"
#include "../test.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <math.h>
#define API(expr) do{bool ok=(expr);if(!ok)fprintf(stderr,"API failure: %s\n",error.message);CHECK(ok);}while(0)
static PPError error;
static PPAssets *assets;
static PPProject *project;
static PPScene *scene;
static PPAnchorPresenter *presenter;
static PPRenderer *renderer;
static PPDisplay *displays;
static size_t display_count;
static PROCESS_INFORMATION child;
static ReceiverState *receiver;
static HANDLE mapping;
static HWND old_foreground;
static POINT old_cursor;
static SDL_Window *settings,*drawer;
static bool left_held,right_held,alt_held,no_displays;
static unsigned downs[3],ups[3],moves[3],wheels[3];
bool pp_test_desktop_capture_start(RECT rectangle);
unsigned char *pp_test_desktop_capture(RECT rectangle,bool require_new);
void pp_test_replacement_reject_output(bool reject);
void pp_test_replacement_reject_source(void);
bool pp_test_replacement_candidates_released(void);
unsigned pp_test_replacement_source_allocations(void);
unsigned pp_test_replacement_output_rejections(void);
unsigned pp_test_replacement_candidate_deletions(void);
static double now(void){LARGE_INTEGER n,f;QueryPerformanceCounter(&n);QueryPerformanceFrequency(&f);return(double)n.QuadPart/f.QuadPart;}
static void cleanup(void){
  pp_platform_settings_window(0);pp_platform_drawer_close(false);SDL_DestroyWindow(drawer);SDL_DestroyWindow(settings);
  pp_anchor_presenter_destroy(presenter);pp_renderer_destroy(renderer);pp_scene_destroy(scene);pp_project_destroy(project);pp_assets_destroy(assets);pp_displays_free(displays);
  INPUT input[2]={{0}};input[0].type=INPUT_MOUSE;input[0].mi.dwFlags=MOUSEEVENTF_LEFTUP|MOUSEEVENTF_RIGHTUP;
  input[1].type=INPUT_KEYBOARD;input[1].ki.wVk=VK_MENU;input[1].ki.dwFlags=KEYEVENTF_KEYUP;
  if(left_held||right_held||alt_held)SendInput(2,input,sizeof(INPUT));
  if(receiver&&receiver->window)PostMessageW((HWND)receiver->window,WM_CLOSE,0,0);
  if(child.hProcess){if(WaitForSingleObject(child.hProcess,3000)!=WAIT_OBJECT_0)TerminateProcess(child.hProcess,1);CloseHandle(child.hProcess);CloseHandle(child.hThread);}
  if(receiver)UnmapViewOfFile(receiver);if(mapping)CloseHandle(mapping);SetCursorPos(old_cursor.x,old_cursor.y);if(old_foreground)SetForegroundWindow(old_foreground);
}
static void poll(void){
  PPPlatformEvent event;while(pp_platform_poll(&event)){
    if(event.instance_id>0&&event.instance_id<3){unsigned id=(unsigned)event.instance_id;
      if(event.kind==PP_EVENT_BUTTON_DOWN&&event.button==PP_BUTTON_LEFT)downs[id]++;
      if(event.kind==PP_EVENT_BUTTON_UP&&event.button==PP_BUTTON_LEFT)ups[id]++;
      if(event.kind==PP_EVENT_POINTER_MOVE)moves[id]++;
      if(event.kind==PP_EVENT_WHEEL)wheels[id]++;
    }
    if(presenter)API(pp_anchor_presenter_event(presenter,&event,&error));
  }
  if(presenter)API(pp_anchor_presenter_update(presenter,no_displays?NULL:displays,no_displays?0:display_count,(PPVec2){300,300},now(),&error));
}
static void pump(void){ULONGLONG until=GetTickCount64()+80;do{poll();Sleep(1);}while(GetTickCount64()<until);}
typedef struct Injection{INPUT input;UINT sent;}Injection;
static DWORD WINAPI inject(void *data){Injection *request=data;request->sent=SendInput(1,&request->input,sizeof(INPUT));return 0;}
static void send_injection(Injection *request){
  HANDLE thread=CreateThread(NULL,0,inject,request,0,NULL);CHECK(thread);ULONGLONG until=GetTickCount64()+3000;
  while(WaitForSingleObject(thread,0)==WAIT_TIMEOUT&&GetTickCount64()<until){poll();Sleep(1);}
  CHECK(WaitForSingleObject(thread,0)==WAIT_OBJECT_0);CloseHandle(thread);CHECK(request->sent==1);pump();
}
static void mouse(DWORD flags,DWORD data){
  if(flags&MOUSEEVENTF_LEFTDOWN)left_held=true;if(flags&MOUSEEVENTF_RIGHTDOWN)right_held=true;
  Injection request={0};request.input.type=INPUT_MOUSE;request.input.mi.dwFlags=flags;request.input.mi.mouseData=data;send_injection(&request);
  if(flags&MOUSEEVENTF_LEFTUP)left_held=false;if(flags&MOUSEEVENTF_RIGHTUP)right_held=false;
}
static void key(bool held){alt_held=true;Injection request={0};request.input.type=INPUT_KEYBOARD;request.input.ki.wVk=VK_MENU;request.input.ki.dwFlags=held?0:KEYEVENTF_KEYUP;send_injection(&request);alt_held=held;}
static void move(int x,int y){
  Injection request={0};request.input.type=INPUT_MOUSE;request.input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
  request.input.mi.dx=MulDiv(x-GetSystemMetrics(SM_XVIRTUALSCREEN),65535,GetSystemMetrics(SM_CXVIRTUALSCREEN)-1);
  request.input.mi.dy=MulDiv(y-GetSystemMetrics(SM_YVIRTUALSCREEN),65535,GetSystemMetrics(SM_CYVIRTUALSCREEN)-1);send_injection(&request);
}
static void click(int x,int y){CHECK(SetCursorPos(x,y));mouse(MOUSEEVENTF_LEFTDOWN,0);mouse(MOUSEEVENTF_LEFTUP,0);}
static PPItem image(const char *name){
  char path[1024];snprintf(path,sizeof(path),"%s/%s",PP_MEDIA_FIXTURES,name);size_t count=0;void *bytes=SDL_LoadFile(path,&count);CHECK(bytes&&count);
  PPItem item={.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=1};API(pp_asset_import(assets,(PPBytes){bytes,count},PP_ASSET_PNG,&item.image_asset,&error));SDL_free(bytes);return item;
}
static HWND native(PPSurface *surface){SDL_Window *window=SDL_GetWindowFromID(pp_surface_window_id(surface));CHECK(window);return SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,NULL);}
static void pixel(const char *name,int x,int y,int r,int g,int b){
  pump();unsigned char *sample=pp_test_desktop_capture((RECT){x,y,x+1,y+1},false);CHECK(sample);
  COLORREF value=RGB(sample[2],sample[1],sample[0]);free(sample);
  printf("PNG compositor %s RGB=%d,%d,%d expected=%d,%d,%d\n",name,GetRValue(value),GetGValue(value),GetBValue(value),r,g,b);
  CHECK(abs((int)GetRValue(value)-r)<=3&&abs((int)GetGValue(value)-g)<=3&&abs((int)GetBValue(value)-b)<=3);
}
static void pose(uint64_t id,PPPose value){API(pp_scene_set_pose(scene,id,value,&error));pump();}
static void unchanged(PPRendererMetrics before,PPSourceMetrics source){PPRendererMetrics after=pp_anchor_presenter_renderer_metrics(presenter);CHECK(after.transform_draws==before.transform_draws&&after.source_uploads==before.source_uploads&&after.readbacks==before.readbacks);CHECK(pp_assets_source_metrics(assets).raster_builds==source.raster_builds);}
static SDL_Window *ui(const char *name){SDL_Window *window=SDL_CreateWindow(name,80,80,SDL_WINDOW_HIDDEN|SDL_WINDOW_BORDERLESS|SDL_WINDOW_NOT_FOCUSABLE|SDL_WINDOW_ALWAYS_ON_TOP);CHECK(window);CHECK(SDL_SetWindowPosition(window,260,260));CHECK(SDL_ShowWindow(window));return window;}
int main(int argc,char **argv){
  CHECK(argc==2||(argc==3&&(!strcmp(argv[2],"--output-exhaustion")||!strcmp(argv[2],"--source-exhaustion"))));
  bool output_exhaustion=argc==3&&!strcmp(argv[2],"--output-exhaustion"),source_exhaustion=argc==3&&!strcmp(argv[2],"--source-exhaustion");
  setvbuf(stdout,NULL,_IONBF,0);old_foreground=GetForegroundWindow();CHECK(GetCursorPos(&old_cursor));atexit(cleanup);SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  char name[80];snprintf(name,sizeof(name),"Local\\PointyPNGReceiver-%lu",GetCurrentProcessId());mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(ReceiverState),name);CHECK(mapping);
  receiver=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(*receiver));CHECK(receiver);char command[2048];snprintf(command,sizeof(command),"\"%s\" %s",argv[1],name);STARTUPINFOA startup={sizeof(startup)};
  CHECK(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&child));ULONGLONG until=GetTickCount64()+4000;while(!receiver->window&&GetTickCount64()<until)Sleep(10);CHECK(receiver->window);
  do{SetForegroundWindow((HWND)receiver->window);pump();}while(GetForegroundWindow()!=(HWND)receiver->window&&GetTickCount64()<until);CHECK(GetForegroundWindow()==(HWND)receiver->window);
  API(pp_assets_create(&assets,&error));API(pp_project_create(assets,&project,&error));API(pp_scene_create(&scene,&error));PPItem item=image("alpha.png");API(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&item,NULL,&error));
  API(pp_renderer_create(&renderer,&error));API(pp_platform_displays(&displays,&display_count,&error));
  CHECK(pp_test_desktop_capture_start((RECT){220,220,500,420}));
  API(pp_anchor_presenter_create_shared(&presenter,renderer,project,scene,&error));pump();
  PPSprite *created=pp_sprites_create(presenter,PP_ANCHOR_TOP_LEFT,(PPPose){{300,300},96,0,1},now(),&error);CHECK(created);uint64_t id=created->id;PPSurface *surface=created->surface;pump();HWND window=native(surface);RECT rectangle;CHECK(GetWindowRect(window,&rectangle));
  printf("PNG native origin=%ld,%ld bounds=%ldx%ld visible=%d alpha=%d draws=%llu source=3x1 density=%.2f retained_gpu=%zu\n",rectangle.left,rectangle.top,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,IsWindowVisible(window),created->retained.visual_alpha,(unsigned long long)pp_anchor_presenter_renderer_metrics(presenter).transform_draws,pp_display_at(displays,display_count,(PPVec2){300,300})->drawable_pixels_per_dip,pp_anchor_presenter_gpu_resources(presenter).retained_bytes);
  pixel("transparent padding",300,300,255,255,255);pixel("premultiplied source",rectangle.left+5,300,255,191,159);
  unsigned before=downs[id];LONG below=receiver->clicks;click(300,300);CHECK(downs[id]==before+1&&receiver->clicks==below&&GetForegroundWindow()==(HWND)receiver->window);click(rectangle.right+1,300);CHECK(receiver->clicks==below+1);
  PPRendererMetrics warm=pp_anchor_presenter_renderer_metrics(presenter);PPSourceMetrics source=pp_assets_source_metrics(assets);pump();unchanged(warm,source);pose(id,(PPPose){{320,300},96,0,1});CHECK(native(surface)==window);unchanged(warm,source);
  before=ups[id];unsigned move_before=moves[id];CHECK(SetCursorPos(320,300));mouse(MOUSEEVENTF_LEFTDOWN,0);move(430,350);mouse(MOUSEEVENTF_LEFTUP,0);CHECK(ups[id]==before+1&&moves[id]>move_before&&pp_scene_get(scene,id)->pose.center.x==430&&GetForegroundWindow()==(HWND)receiver->window);unchanged(warm,source);
  pose(id,(PPPose){{300,300},96,0,.5});CHECK(GetWindowRect(window,&rectangle));pixel("display opacity",rectangle.left+5,300,255,223,207);
  CHECK(SetCursorPos(300,300));unsigned wheel_before=wheels[id];key(true);mouse(MOUSEEVENTF_WHEEL,WHEEL_DELTA);key(false);pump();CHECK(wheels[id]==wheel_before+1&&pp_scene_get(scene,id)->pose.longest_dip>96);CHECK(pp_anchor_presenter_renderer_metrics(presenter).source_uploads==warm.source_uploads&&pp_anchor_presenter_renderer_metrics(presenter).readbacks==0);
  pose(id,(PPPose){{300,300},96,37,1});CHECK(GetWindowRect(window,&rectangle));pixel("rotated empty corner",rectangle.left+2,rectangle.top+2,255,255,255);before=downs[id];click(rectangle.left+2,rectangle.top+2);CHECK(downs[id]==before+1);
  pose(id,(PPPose){{300,300},96,0,1});CHECK(SetCursorPos(300,300));mouse(MOUSEEVENTF_LEFTDOWN,0);pose(id,(PPPose){{300,300},96,0,0});mouse(MOUSEEVENTF_LEFTUP,0);CHECK(!IsWindowVisible(window));below=receiver->clicks;click(300,300);CHECK(receiver->clicks==below+1);
  pose(id,(PPPose){{300,300},96,0,1});CHECK(SetCursorPos(300,300));mouse(MOUSEEVENTF_LEFTDOWN,0);no_displays=true;pump();mouse(MOUSEEVENTF_LEFTUP,0);CHECK(!IsWindowVisible(window));below=receiver->clicks;click(300,300);CHECK(receiver->clicks==below+1);no_displays=false;pump();CHECK(IsWindowVisible(window));
  puts("PASS native PNG rectangle/alpha, original outside input, static/translation reuse, drag outside, cached opacity/wheel/rotation, zero opacity and hidden capture cleanup");
  PPItem blue=image("retained-blue.png");
  /* The 8x4 candidate source fits in 128 bytes, but its changed aspect needs
   * a new output while the accepted 3x1 source and output remain charged. */
  PPSprite *accepted=pp_sprites_find(presenter,id);CHECK(accepted);
  PPItemSnapshot *snapshot=pp_item_snapshot_retain(pp_scene_get(scene,id)->snapshot);
  PPPose accepted_pose=pp_scene_get(scene,id)->pose;double start=pp_scene_get(scene,id)->animation_start;
  PPRetainedPresentation retained=accepted->retained;CHECK(GetWindowRect(window,&rectangle));
  CHECK(SetCursorPos(500,450));pump();
  unsigned char *artwork=pp_test_desktop_capture(rectangle,true);CHECK(artwork);
  size_t charge=pp_anchor_presenter_gpu_resources(presenter).retained_bytes;
  PPRendererMetrics rejected_before=pp_anchor_presenter_renderer_metrics(presenter);
  if(output_exhaustion) {
    CHECK(pp_display_at(displays,display_count,accepted_pose.center)->drawable_pixels_per_dip==1.5);
    pp_test_replacement_reject_output(true);
  } else if(source_exhaustion)pp_test_replacement_reject_source();
  else presenter->gpu.limit_bytes=charge+128;
  API(pp_scene_replace(scene,id,&blue,now(),&error));pump();
  CHECK(pp_scene_get(scene,id)&&pp_scene_count(scene)==1&&presenter->instance_count==1);
  accepted=pp_sprites_find(presenter,id);CHECK(accepted&&accepted->surface==surface&&accepted->retained.valid);
  CHECK(presenter->notice.code==(output_exhaustion||source_exhaustion?PP_ERROR_MEMORY:PP_ERROR_RESOURCE_LIMIT)&&!presenter->failed_surface);
  if(output_exhaustion) {
    CHECK(pp_test_replacement_source_allocations()==1&&pp_test_replacement_output_rejections()==1);
    CHECK(pp_test_replacement_candidate_deletions()==2);
    CHECK(pp_anchor_presenter_gpu_resources(presenter).peak_bytes>=charge+128+144*72*4);
  }
  if(source_exhaustion)CHECK(pp_test_replacement_source_allocations()==0&&pp_test_replacement_output_rejections()==0&&pp_test_replacement_candidate_deletions()==1);
  if(output_exhaustion||source_exhaustion) {
    API(pp_renderer_complete(presenter->texture_owner,&error));CHECK(pp_test_replacement_candidates_released());
  }
  CHECK(pp_scene_get(scene,id)->snapshot==snapshot&&accepted->retained.snapshot==snapshot);
  CHECK(!memcmp(&pp_scene_get(scene,id)->pose,&accepted_pose,sizeof(accepted_pose)));
  CHECK(!memcmp(&accepted->pose,&accepted_pose,sizeof(accepted_pose)));
  CHECK(pp_scene_get(scene,id)->animation_start==start&&accepted->retained.animation_start==start);
  CHECK(accepted->retained.content_id==retained.content_id&&accepted->retained.source_revision==retained.source_revision);
  CHECK(accepted->retained.source_width==3&&accepted->retained.source_height==1);
  CHECK(!memcmp(&accepted->retained.drawn,&retained.drawn,sizeof(retained.drawn))&&accepted->retained.density==retained.density);
  CHECK(accepted->retained.gpu_bytes==retained.gpu_bytes&&accepted->retained.output_bytes==retained.output_bytes);
  CHECK(pp_anchor_presenter_gpu_resources(presenter).retained_bytes==charge);
  RECT rejected_bounds;CHECK(GetWindowRect(window,&rejected_bounds));CHECK(!memcmp(&rectangle,&rejected_bounds,sizeof(rectangle))&&IsWindowVisible(window));
  /* Ask the receiver to repaint so the assertion cannot reuse a cached desktop
   * image. This does not change or redraw the accepted sprite. */
  CHECK(RedrawWindow((HWND)receiver->window,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW));pump();
  unsigned char *after=pp_test_desktop_capture(rectangle,true);CHECK(after);
  size_t pixels=(size_t)(rectangle.right-rectangle.left)*(rectangle.bottom-rectangle.top);
  for(size_t i=0;i<pixels;i++)for(size_t c=0;c<3;c++)CHECK(abs(after[i*4+c]-artwork[i*4+c])<=3);
  free(after);free(artwork);
  PPRendererMetrics rejected_after=pp_anchor_presenter_renderer_metrics(presenter);
  CHECK(rejected_after.source_uploads==rejected_before.source_uploads&&rejected_after.transform_draws==rejected_before.transform_draws);
  before=downs[id];below=receiver->clicks;click(300,300);CHECK(downs[id]==before+1&&receiver->clicks==below&&GetForegroundWindow()==(HWND)receiver->window);
  if(output_exhaustion||source_exhaustion) {
    /* Force an accepted-source transform after rejection. A cached old output
     * alone cannot satisfy this check if the old source was already retired. */
    PPPose redraw=accepted_pose;redraw.opacity=.5;pose(id,redraw);pose(id,accepted_pose);
    CHECK(GetWindowRect(window,&rejected_bounds));CHECK(!memcmp(&rectangle,&rejected_bounds,sizeof(rectangle)));
    pixel("accepted source redraw after rejection",rectangle.left+5,300,255,191,159);
    CHECK(pp_scene_get(scene,id)->snapshot==snapshot&&pp_anchor_presenter_gpu_resources(presenter).retained_bytes==charge);
  }
  presenter->gpu.limit_bytes=PP_RETAINED_GPU_LIMIT;
  pp_test_replacement_reject_output(false);
  CHECK(SetCursorPos(300,300));API(pp_scene_replace(scene,id,&blue,now(),&error));pump();pixel("stationary immutable replacement",300,300,0,0,255);CHECK(native(surface)==window);
  CHECK(pp_scene_get(scene,id)->snapshot!=snapshot&&pp_scene_get(scene,id)->animation_start>start);
  accepted=pp_sprites_find(presenter,id);CHECK(accepted->retained.source_width==8&&accepted->retained.source_height==4);
  CHECK(pp_anchor_presenter_gpu_resources(presenter).retained_bytes==charge-retained.gpu_bytes-retained.output_bytes+accepted->retained.gpu_bytes+accepted->retained.output_bytes);
  CHECK(pp_anchor_presenter_gpu_resources(presenter).peak_bytes>=charge+128+pp_sprites_find(presenter,id)->retained.output_bytes);
  pp_item_snapshot_release(snapshot);
  printf("PASS replacement %s rejection preserves sprite, snapshot, pose, clock, native bounds, all displayed pixels, GPU charge and input; retry commits\n",
    output_exhaustion?"driver output":source_exhaustion?"driver source":"application budget");
  PPItem empty=image("retained-empty.png");mouse(MOUSEEVENTF_LEFTDOWN,0);API(pp_scene_replace(scene,id,&empty,now(),&error));pump();mouse(MOUSEEVENTF_LEFTUP,0);CHECK(!IsWindowVisible(window)&&pp_scene_get(scene,id)->pose.opacity==1);below=receiver->clicks;click(300,300);CHECK(receiver->clicks==below+1);API(pp_scene_replace(scene,id,&blue,now(),&error));pump();CHECK(IsWindowVisible(window));
  puts("PASS fully transparent PNG source at opacity1 releases input/capture, with source visibility retained between updates");
  created=pp_sprites_create(presenter,PP_ANCHOR_TOP_LEFT,(PPPose){{300,300},96,0,1},now(),&error);CHECK(created);uint64_t second=created->id;pump();before=downs[second];click(300,300);CHECK(downs[second]==before+1);
  settings=ui("PNG Settings priority fixture");pp_platform_settings_window(SDL_GetWindowID(settings));drawer=ui("PNG drawer priority fixture");pp_platform_drawer_window(SDL_GetWindowID(drawer));pump();before=downs[id]+downs[second];below=receiver->clicks;click(300,300);CHECK(downs[id]+downs[second]==before&&receiver->clicks==below);
  pose(id,(PPPose){{300,300},96,0,0});pose(id,(PPPose){{300,300},96,0,1});pp_surface_raise(surface);CHECK(WindowFromPoint((POINT){300,300})==SDL_GetPointerProperty(SDL_GetWindowProperties(drawer),SDL_PROP_WINDOW_WIN32_HWND_POINTER,NULL));
  pp_platform_drawer_close(false);SDL_HideWindow(settings);pp_platform_settings_window(0);SDL_DestroyWindow(drawer);drawer=NULL;SDL_DestroyWindow(settings);settings=NULL;pump();pp_surface_raise(pp_sprites_find(presenter,second)->surface);CHECK(SetCursorPos(300,300));LONG right=receiver->right_ups,menus=receiver->context_menus;mouse(MOUSEEVENTF_RIGHTDOWN,0);
  CHECK(!pp_scene_get(scene,second)&&pp_scene_get(scene,id));CHECK(SetCursorPos(430,350));mouse(MOUSEEVENTF_RIGHTUP,0);CHECK(receiver->right_ups==right&&receiver->context_menus==menus);mouse(MOUSEEVENTF_RIGHTDOWN,0);mouse(MOUSEEVENTF_RIGHTUP,0);CHECK(receiver->right_ups==right+1&&receiver->context_menus==menus+1);
  CHECK(SetCursorPos(300,300));mouse(MOUSEEVENTF_LEFTDOWN,0);CHECK(DestroyWindow(window));pose(id,(PPPose){{300,300},96,20,1});mouse(MOUSEEVENTF_LEFTUP,0);CHECK(!pp_scene_get(scene,id)&&pp_anchor_presenter_gpu_resources(presenter).retained_bytes==0);below=receiver->clicks;click(300,300);CHECK(receiver->clicks==below+1&&GetForegroundWindow()==(HWND)receiver->window);
  created=pp_sprites_create(presenter,PP_ANCHOR_TOP_LEFT,(PPPose){{300,300},96,0,1},now(),&error);CHECK(created);id=created->id;pump();
  CHECK(SetCursorPos(300,300));mouse(MOUSEEVENTF_LEFTDOWN,0);
  API(pp_renderer_complete(created->retained.renderer,&error));glEnable(0); /* Real GL_INVALID_ENUM, on the owning context. */
  created->pose.angle_deg=17.25;created->dirty=true;API(pp_sprites_update(presenter,now(),&error));
  CHECK(presenter->notice.code==PP_ERROR_RENDER&&!pp_scene_get(scene,id)&&!pp_anchor_presenter_gpu_resources(presenter).retained_bytes);
  mouse(MOUSEEVENTF_LEFTUP,0);below=receiver->clicks;click(300,300);CHECK(receiver->clicks==below+1);
  puts("PASS real GL error removes failed sprite and releases accepted press, capture, GPU source and invisible interception");
  PPRendererMetrics total=pp_anchor_presenter_renderer_metrics(presenter);printf("PNG totals uploads=%llu draws=%llu readbacks=%llu GPU peak=%zu remaining=%zu\n",(unsigned long long)total.source_uploads,(unsigned long long)total.transform_draws,(unsigned long long)total.readbacks,pp_anchor_presenter_gpu_resources(presenter).peak_bytes,pp_anchor_presenter_gpu_resources(presenter).retained_bytes);CHECK(total.readbacks==0);
  puts("PASS native PNG immutable replacement, overlap/UI order, right deletion drain, destroyed-window failure cleanup and nonactivation; macOS runtime unverified");return 0;
}
