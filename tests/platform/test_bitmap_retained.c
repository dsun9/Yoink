#include "../../src/app/presenter_internal.h"
#include "receiver_shared.h"
#include "../test.h"
#include <SDL3/SDL.h>
#include <math.h>
#define API(expr) do { bool ok=(expr); if(!ok) fprintf(stderr,"API failure: %s\n",error.message); CHECK(ok); } while(0)
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
static bool left_held,alt_held;
static unsigned downs,ups,moves,wheels;
static double now(void) { LARGE_INTEGER n,f;QueryPerformanceCounter(&n);QueryPerformanceFrequency(&f);return (double)n.QuadPart/f.QuadPart; }
static void cleanup(void) {
  pp_anchor_presenter_destroy(presenter);pp_renderer_destroy(renderer);pp_scene_destroy(scene);pp_project_destroy(project);pp_assets_destroy(assets);pp_displays_free(displays);
  INPUT input[2]={{0}};input[0].type=INPUT_MOUSE;input[0].mi.dwFlags=MOUSEEVENTF_LEFTUP|MOUSEEVENTF_RIGHTUP;
  input[1].type=INPUT_KEYBOARD;input[1].ki.wVk=VK_MENU;input[1].ki.dwFlags=KEYEVENTF_KEYUP;
  if(left_held||alt_held) SendInput(2,input,sizeof(INPUT));
  if(receiver&&receiver->window) PostMessageW((HWND)receiver->window,WM_CLOSE,0,0);
  if(child.hProcess) { if(WaitForSingleObject(child.hProcess,3000)!=WAIT_OBJECT_0) TerminateProcess(child.hProcess,1);CloseHandle(child.hProcess);CloseHandle(child.hThread); }
  if(receiver) UnmapViewOfFile(receiver);if(mapping) CloseHandle(mapping);SetCursorPos(old_cursor.x,old_cursor.y);if(old_foreground) SetForegroundWindow(old_foreground);
}
static void poll(void) {
  PPPlatformEvent event;while(pp_platform_poll(&event)) {
    if(event.kind==PP_EVENT_WHEEL)printf("WHEEL instance=%llu modifiers=%u delta=%.3f at=%.0f,%.0f\n",(unsigned long long)event.instance_id,event.modifiers,event.wheel_delta,event.desktop_point.x,event.desktop_point.y);
    if(event.instance_id) { downs+=event.kind==PP_EVENT_BUTTON_DOWN;ups+=event.kind==PP_EVENT_BUTTON_UP;moves+=event.kind==PP_EVENT_POINTER_MOVE;wheels+=event.kind==PP_EVENT_WHEEL; }
    if(presenter) API(pp_anchor_presenter_event(presenter,&event,&error));
  }
  if(presenter) API(pp_anchor_presenter_update(presenter,displays,display_count,(PPVec2){500,400},now(),&error));
}
static void pump(void) { ULONGLONG until=GetTickCount64()+180;while(GetTickCount64()<until) {poll();Sleep(2);} }
static void mouse(DWORD flags,DWORD data) { INPUT input={0};input.type=INPUT_MOUSE;input.mi.dwFlags=flags;input.mi.mouseData=data;CHECK(SendInput(1,&input,sizeof(INPUT))==1);if(flags==MOUSEEVENTF_LEFTDOWN)left_held=true;if(flags==MOUSEEVENTF_LEFTUP)left_held=false;pump(); }
static void click(int x,int y) { CHECK(SetCursorPos(x,y));mouse(MOUSEEVENTF_LEFTDOWN,0);mouse(MOUSEEVENTF_LEFTUP,0); }
static void key(bool down) { INPUT input={0};input.type=INPUT_KEYBOARD;input.ki.wVk=VK_MENU;input.ki.dwFlags=down?0:KEYEVENTF_KEYUP;CHECK(SendInput(1,&input,sizeof(INPUT))==1);alt_held=down;pump(); }
static HWND native(PPSurface *surface) { SDL_Window *window=SDL_GetWindowFromID(pp_surface_window_id(surface));CHECK(window);return SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,NULL); }
static void unchanged(PPRendererMetrics before,uint64_t builds) {
  PPRendererMetrics after=pp_anchor_presenter_renderer_metrics(presenter);
  CHECK(after.source_uploads==before.source_uploads&&after.transform_draws==before.transform_draws&&after.readbacks==before.readbacks);
  CHECK(pp_assets_source_metrics(assets).raster_builds==builds);
}
static void pose(uint64_t id,PPPose value) { API(pp_scene_set_pose(scene,id,value,&error));pump(); }
static int clamp(int value,int count) { return value<0?0:value>=count?count-1:value; }
static void quality(uint64_t id,const PPTextResult *source) {
  CHECK(GetForegroundWindow()==(HWND)receiver->window);
  PPSprite *sprite=pp_sprites_find(presenter,id);CHECK(sprite);RECT rectangle;CHECK(GetWindowRect(native(sprite->surface),&rectangle));
  const PPDisplay *display=pp_display_at(displays,display_count,sprite->pose.center);CHECK(display);
  PPRect source_rect={source->layout_origin.x-source->raster_origin_px.x/source->raster_density,
    source->layout_origin.y-source->raster_origin_px.y/source->raster_density,
    source->frame.width/source->raster_density,source->frame.height/source->raster_density};
  PPVec2 layout=source->layout_size;double density=display->drawable_pixels_per_dip;
  PPPose expected_pose=pp_scene_get(scene,id)->pose;
  double drawable_per_desktop=density/display->desktop_units_per_dip,scale=expected_pose.longest_dip*density/fmax(layout.x,layout.y);
  double angle=expected_pose.angle_deg*3.14159265358979323846/180,c=cos(angle),s=sin(angle);
  bool pp_test_desktop_capture_start(RECT rectangle);
  unsigned char *pp_test_desktop_capture(RECT rectangle,bool require_new);
  static bool capture_started;
  if(!capture_started){CHECK(pp_test_desktop_capture_start(rectangle));capture_started=true;Sleep(100);}
  int capture_width=rectangle.right-rectangle.left;
  unsigned char *capture_pixels=pp_test_desktop_capture(rectangle,false);CHECK(capture_pixels);
  unsigned samples=0,partial=0,max_error=0;
  COLORREF background=GetSysColor(COLOR_WINDOW);int backdrop[3]={GetRValue(background),GetGValue(background),GetBValue(background)};
  int step_x=SDL_max(7,(rectangle.right-rectangle.left+23)/24),step_y=SDL_max(7,(rectangle.bottom-rectangle.top+23)/24);
  for(int py=rectangle.top+2;py<rectangle.bottom-2;py+=step_y) for(int px=rectangle.left+2;px<rectangle.right-2;px+=step_x) {
    double dx=(px+.5-expected_pose.center.x)*drawable_per_desktop;
    double dy=(py+.5-expected_pose.center.y)*drawable_per_desktop;
    double local_x=(c*dx+s*dy)/scale+layout.x*.5,local_y=(-s*dx+c*dy)/scale+layout.y*.5;
    double sx=(local_x-source_rect.x)/source_rect.w*source->frame.width,sy=(local_y-source_rect.y)/source_rect.h*source->frame.height;
    double rgba[4]={0};
    if(sx>=0&&sy>=0&&sx<=source->frame.width&&sy<=source->frame.height) {
      sx-=.5;sy-=.5;int x=(int)floor(sx),y=(int)floor(sy);double fx=sx-x,fy=sy-y;
      for(int channel=0;channel<4;channel++) {
        const uint8_t *pixels=source->frame.pixels;size_t stride=source->frame.stride;
        double top=pixels[(size_t)clamp(y,source->frame.height)*stride+clamp(x,source->frame.width)*4+channel]*(1-fx)+pixels[(size_t)clamp(y,source->frame.height)*stride+clamp(x+1,source->frame.width)*4+channel]*fx;
        double bottom=pixels[(size_t)clamp(y+1,source->frame.height)*stride+clamp(x,source->frame.width)*4+channel]*(1-fx)+pixels[(size_t)clamp(y+1,source->frame.height)*stride+clamp(x+1,source->frame.width)*4+channel]*fx;
        rgba[channel]=(top*(1-fy)+bottom*fy)*expected_pose.opacity;
      }
    }
    const unsigned char *pixel=capture_pixels+((size_t)(py-rectangle.top)*capture_width+px-rectangle.left)*4;int actual[3]={pixel[2],pixel[1],pixel[0]};
    for(int channel=0;channel<3;channel++) {
      int expected=(int)lround(rgba[channel]+backdrop[channel]*(1-rgba[3]/255));unsigned difference=(unsigned)abs(actual[channel]-expected);
      if(difference>max_error) max_error=difference;
      if(difference>4) fprintf(stderr,"bitmap color at%d,%d channel%d actual%d expected%d source%.3f,%.3f angle%.2f\n",px,py,channel,actual[channel],expected,sx,sy,sprite->pose.angle_deg);
      CHECK(difference<=4);
    }
    partial+=rgba[3]>0&&rgba[3]<254;samples++;
  }
  free(capture_pixels);CHECK(samples>100&&partial>0);CHECK(GetForegroundWindow()==(HWND)receiver->window);
  printf("QUALITY native bitmap original-strike bilinear samples=%u partial_alpha=%u max_RGB_error=%u rotation=%.2f opacity=%.2f density=%.2f layout=%.0fx%.0f source=%dx%d\n",samples,partial,max_error,sprite->pose.angle_deg,sprite->pose.opacity,density,layout.x,layout.y,source->frame.width,source->frame.height);
}
int main(int argc,char **argv) {
  CHECK(argc==2);setvbuf(stdout,NULL,_IONBF,0);old_foreground=GetForegroundWindow();CHECK(GetCursorPos(&old_cursor));atexit(cleanup);SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  char name[80];snprintf(name,sizeof(name),"Local\\PointyBitmapReceiver-%lu",GetCurrentProcessId());mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(ReceiverState),name);CHECK(mapping);
  receiver=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(*receiver));CHECK(receiver);char command[2048];snprintf(command,sizeof(command),"\"%s\" %s",argv[1],name);STARTUPINFOA startup={sizeof(startup)};
  CHECK(CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&child));ULONGLONG until=GetTickCount64()+4000;while(!receiver->window&&GetTickCount64()<until)Sleep(10);CHECK(receiver->window);
  SetWindowLongPtrW((HWND)receiver->window,GWL_STYLE,WS_POPUP|WS_VISIBLE);
  CHECK(SetWindowPos((HWND)receiver->window,NULL,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),SWP_NOZORDER|SWP_FRAMECHANGED));
  do {SetForegroundWindow((HWND)receiver->window);pump();} while(GetForegroundWindow()!=(HWND)receiver->window&&GetTickCount64()<until);CHECK(GetForegroundWindow()==(HWND)receiver->window);
  API(pp_assets_create(&assets,&error));API(pp_project_create(assets,&project,&error));API(pp_scene_create(&scene,&error));size_t length=0;void *bytes=SDL_LoadFile(PP_RESOURCES "/fonts/NotoColorEmoji.ttf",&length);CHECK(bytes&&length);
  PPItem item={.kind=PP_ITEM_TEXT,.text="\xf0\x9f\x91\x89",.opacity=1,.text_style={.requested_family="Noto Color Emoji",.font_size_dip=96,.color={0,0,0,255}}};
  API(pp_asset_import(assets,(PPBytes){bytes,length},PP_ASSET_FONT,&item.text_style.fallback_asset,&error));SDL_free(bytes);uint64_t item_id=0;API(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&item,&item_id,&error));
  API(pp_renderer_create(&renderer,&error));API(pp_platform_displays(&displays,&display_count,&error));API(pp_anchor_presenter_create_shared(&presenter,renderer,project,scene,&error));pump();
  PPSprite *created=pp_sprites_create(presenter,PP_ANCHOR_TOP_LEFT,(PPPose){{500,400},120,0,1},now(),&error);CHECK(created);uint64_t id=created->id;pump();PPSurface *surface=pp_sprites_find(presenter,id)->surface;HWND window=native(surface);
  const PPTextResult *source=NULL;API(pp_item_snapshot_bitmap_source(pp_scene_get(scene,id)->snapshot,&source,&error));CHECK(source->frame.width==136&&source->frame.height==128&&!source->frame.content_alpha);
  printf("BITMAP native density=%.2f source=136x128 canonical=120x113 retained_GPU=%zu\n",pp_display_at(displays,display_count,(PPVec2){500,400})->drawable_pixels_per_dip,pp_anchor_presenter_gpu_resources(presenter).retained_bytes);
  quality(id,source);
  PPItemSnapshot *original=pp_item_snapshot_retain(pp_scene_get(scene,id)->snapshot);
  for(int change=0;change<2;change++) {
    item.id=item_id;item.text_style.font_size_dip=change?96:192;
    API(pp_project_edit_item(project,item_id,&item,&error));
    PPItemSnapshot *replacement=pp_project_snapshot(project,item_id);const PPTextResult *fresh=NULL;
    API(pp_item_snapshot_bitmap_source(replacement,&fresh,&error));
    CHECK(fresh->frame.width==source->frame.width&&fresh->frame.height==source->frame.height&&
      fresh->frame.stride==source->frame.stride&&!memcmp(fresh->frame.pixels,source->frame.pixels,source->frame.stride*source->frame.height));
    for(int rotation=0;rotation<2;rotation++) {
      PPPose preserved={{500,400},384,rotation?17.25:0,.5};pose(id,preserved);
      PPRendererMetrics before=pp_anchor_presenter_renderer_metrics(presenter);
      API(pp_scene_replace_snapshot(scene,id,replacement,now(),&error));pump();
      CHECK(!memcmp(&pp_scene_get(scene,id)->pose,&preserved,sizeof(preserved)));
      CHECK(pp_anchor_presenter_renderer_metrics(presenter).source_uploads==before.source_uploads);
      quality(id,fresh);
      printf("REPLACEMENT bitmap font=%.0f rotation=%.2f pose_preserved=1 original_source_pixels_equal=1 unnecessary_uploads=0\n",item.text_style.font_size_dip,preserved.angle_deg);
    }
    source=fresh;pp_item_snapshot_release(replacement);
  }
  pp_item_snapshot_release(original);pose(id,(PPPose){{500,400},120,0,1});
  PPRendererMetrics warm=pp_anchor_presenter_renderer_metrics(presenter);uint64_t builds=pp_assets_source_metrics(assets).raster_builds;pump();unchanged(warm,builds);
  unsigned before=downs;LONG below=receiver->clicks;click(500,400);CHECK(downs==before+1&&receiver->clicks==below&&GetForegroundWindow()==(HWND)receiver->window);
  RECT rectangle;CHECK(GetWindowRect(window,&rectangle));click(rectangle.right+2,400);CHECK(receiver->clicks==below+1);
  pose(id,(PPPose){{520,400},120,0,1});CHECK(native(surface)==window);unchanged(warm,builds);
  CHECK(SetCursorPos(520,400));unsigned up_before=ups,move_before=moves;mouse(MOUSEEVENTF_LEFTDOWN,0);CHECK(SetCursorPos(700,500));pump();mouse(MOUSEEVENTF_LEFTUP,0);
  CHECK(ups==up_before+1&&moves>move_before&&pp_scene_get(scene,id)->pose.center.x==700&&GetForegroundWindow()==(HWND)receiver->window);unchanged(warm,builds);
  pose(id,(PPPose){{500,400},384,17.25,.5});quality(id,source);CHECK(SetCursorPos(500,400));unsigned wheel_before=wheels;key(true);
  POINT wheel_point;CHECK(GetCursorPos(&wheel_point));
  printf("WHEEL target actual=%ld,%ld native=%p expected=%p alt=%d\n",wheel_point.x,wheel_point.y,(void *)WindowFromPoint(wheel_point),(void *)window,GetAsyncKeyState(VK_MENU)<0);
  mouse(MOUSEEVENTF_WHEEL,WHEEL_DELTA);key(false);pump();
  const PPSceneInstance *after_wheel=pp_scene_get(scene,id);
  printf("WHEEL result count=%u expected=%u instance_alive=%d size=%.9f angle=%.9f\n",wheels,wheel_before+1,after_wheel!=NULL,after_wheel?after_wheel->pose.longest_dip:0,after_wheel?after_wheel->pose.angle_deg:0);
  CHECK(wheels==wheel_before+1&&after_wheel&&after_wheel->pose.longest_dip>384);
  CHECK(pp_assets_source_metrics(assets).raster_builds==builds&&pp_anchor_presenter_renderer_metrics(presenter).source_uploads==warm.source_uploads&&pp_anchor_presenter_renderer_metrics(presenter).readbacks==0);
  CHECK(SetCursorPos(500,400));mouse(MOUSEEVENTF_LEFTDOWN,0);pose(id,(PPPose){{500,400},120,0,0});mouse(MOUSEEVENTF_LEFTUP,0);CHECK(!IsWindowVisible(window));below=receiver->clicks;click(500,400);CHECK(receiver->clicks==below+1);
  pose(id,(PPPose){{500,400},120,0,1});CHECK(SetCursorPos(500,400));mouse(MOUSEEVENTF_LEFTDOWN,0);CHECK(DestroyWindow(window));pose(id,(PPPose){{500,400},120,20,1});mouse(MOUSEEVENTF_LEFTUP,0);CHECK(!pp_scene_get(scene,id)&&pp_anchor_presenter_gpu_resources(presenter).retained_bytes==0);
  below=receiver->clicks;click(500,400);CHECK(receiver->clicks==below+1&&GetForegroundWindow()==(HWND)receiver->window);
  puts("PASS native bitmap original-detail/premultiplied colors, rectangular input, outside input, nonactivation, static/translation reuse, capture, fractional transform/wheel, opacity and destroyed-window cleanup; macOS native unverified");return 0;
}
