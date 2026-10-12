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
static HANDLE receiver_job;
static HWND old_foreground;
static POINT old_cursor;
static bool left_held;
static unsigned downs[256],ups[256],moves[256];
static uint64_t measuring_id;
static bool measuring_exposure;
static double ordinary_ms[256],exposure_ms[256];
static unsigned ordinary_count,exposure_count;
unsigned pp_test_exposure_input_wait(void);
unsigned pp_test_exposure_input_count(void);
static double now(void){LARGE_INTEGER n,f;QueryPerformanceCounter(&n);QueryPerformanceFrequency(&f);return(double)n.QuadPart/f.QuadPart;}
static void cleanup(void){
  pp_anchor_presenter_destroy(presenter);pp_renderer_destroy(renderer);pp_scene_destroy(scene);pp_project_destroy(project);pp_assets_destroy(assets);pp_displays_free(displays);
  INPUT input[2]={{0}};input[0].type=INPUT_MOUSE;input[0].mi.dwFlags=MOUSEEVENTF_LEFTUP|MOUSEEVENTF_RIGHTUP;
  input[1].type=INPUT_KEYBOARD;input[1].ki.wVk=VK_MENU;input[1].ki.dwFlags=KEYEVENTF_KEYUP;
  if(left_held)SendInput(2,input,sizeof(INPUT));
  if(receiver&&receiver->window)PostMessageW((HWND)receiver->window,WM_CLOSE,0,0);
  if(child.hProcess){if(WaitForSingleObject(child.hProcess,3000)!=WAIT_OBJECT_0)TerminateProcess(child.hProcess,1);CloseHandle(child.hProcess);CloseHandle(child.hThread);}
  if(receiver)UnmapViewOfFile(receiver);if(mapping)CloseHandle(mapping);SetCursorPos(old_cursor.x,old_cursor.y);if(old_foreground)SetForegroundWindow(old_foreground);
  if(receiver_job)CloseHandle(receiver_job);
}
static void poll(void){
  double start=now();bool moved=false;PPRendererMetrics before=pp_anchor_presenter_renderer_metrics(presenter);
  PPPlatformEvent event;while(pp_platform_poll(&event)){
    if(measuring_id&&event.instance_id==measuring_id&&event.kind==PP_EVENT_POINTER_MOVE)moved=true;
    if(event.instance_id>0&&event.instance_id<256){unsigned id=(unsigned)event.instance_id;
      if(event.kind==PP_EVENT_BUTTON_DOWN&&event.button==PP_BUTTON_LEFT)downs[id]++;
      if(event.kind==PP_EVENT_BUTTON_UP&&event.button==PP_BUTTON_LEFT)ups[id]++;
      if(event.kind==PP_EVENT_POINTER_MOVE)moves[id]++;
    }
    if(presenter)API(pp_anchor_presenter_event(presenter,&event,&error));
  }
  if(presenter)API(pp_anchor_presenter_update(presenter,displays,display_count,(PPVec2){300,300},now(),&error));
  if(moved) {
    PPRendererMetrics after=pp_anchor_presenter_renderer_metrics(presenter);bool recovery=after.presentation_refreshes>before.presentation_refreshes;
    if(recovery)API(pp_renderer_complete(pp_sprites_find(presenter,measuring_id)->retained.renderer,&error));
    double ms=(now()-start)*1000;
    if(measuring_exposure){CHECK(recovery&&exposure_count<256);exposure_ms[exposure_count++]=ms;}
    else {CHECK(!recovery&&ordinary_count<256);ordinary_ms[ordinary_count++]=ms;}
  }
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
  if(flags&MOUSEEVENTF_LEFTDOWN)left_held=true;
  Injection request={0};request.input.type=INPUT_MOUSE;request.input.mi.dwFlags=flags;request.input.mi.mouseData=data;send_injection(&request);
  if(flags&MOUSEEVENTF_LEFTUP)left_held=false;
}
static void move(int x,int y){
  Injection request={0};request.input.type=INPUT_MOUSE;request.input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
  request.input.mi.dx=MulDiv(x-GetSystemMetrics(SM_XVIRTUALSCREEN),65535,GetSystemMetrics(SM_CXVIRTUALSCREEN)-1);
  request.input.mi.dy=MulDiv(y-GetSystemMetrics(SM_YVIRTUALSCREEN),65535,GetSystemMetrics(SM_CYVIRTUALSCREEN)-1);request.input.mi.dwExtraInfo=0x50504558;send_injection(&request);
}
unsigned char *pp_test_desktop_capture(RECT rectangle,bool require_new);
bool pp_test_desktop_capture_start(RECT rectangle);
static HWND native(PPSurface *surface){SDL_Window *window=SDL_GetWindowFromID(pp_surface_window_id(surface));CHECK(window);return SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,NULL);}
static RECT rectangle(uint64_t id){PPSprite *s=pp_sprites_find(presenter,id);CHECK(s);RECT r;CHECK(GetWindowRect(native(s->surface),&r));return r;}
static void compare(uint64_t id,const unsigned char *reference,const char *stage){
  pump();RECT r=rectangle(id);PPRect screen=displays[0].bounds;
  RECT visible={SDL_max(r.left,(LONG)screen.x),SDL_max(r.top,(LONG)screen.y),SDL_min(r.right,(LONG)(screen.x+screen.w)),SDL_min(r.bottom,(LONG)(screen.y+screen.h))};
  CHECK(visible.right>visible.left&&visible.bottom>visible.top);
  unsigned char *pixels=pp_test_desktop_capture(visible,strcmp(stage,"after release")!=0);CHECK(pixels);
  size_t missing=0,width=visible.right-visible.left,count=width*(visible.bottom-visible.top),reference_width=r.right-r.left;
  for(size_t i=0;i<count;i++) {
    size_t original=((visible.top-r.top)+i/width)*reference_width+(visible.left-r.left)+i%width;
    if(abs(pixels[i*4]-reference[original*4])>3||abs(pixels[i*4+1]-reference[original*4+1])>3||abs(pixels[i*4+2]-reference[original*4+2])>3)missing++;
  }
  free(pixels);printf("Desktop comparison %s mismatched_pixels=%zu total=%zu\n",stage,missing,count);CHECK(missing==0);
}
static uint64_t spawn(PPVec2 center,double size){PPSprite *s=pp_sprites_create(presenter,PP_ANCHOR_TOP_LEFT,(PPPose){center,size,0,1},now(),&error);CHECK(s);uint64_t id=s->id;pump();return id;}
static void remove_sprite(uint64_t id){pp_scene_delete(scene,id);pump();}
static void set_position(uint64_t id,PPVec2 center){PPPose pose=pp_scene_get(scene,id)->pose;pose.center=center;API(pp_scene_set_pose(scene,id,pose,&error));pump();}
static void exercise(int side,double size){
  PPRect screen=displays[0].bounds;PPVec2 home={screen.x+screen.w*.5,screen.y+screen.h*.5};
  uint64_t reference_id=spawn(home,size);
  ULONGLONG until=GetTickCount64()+2000;
  do {pump();}while(pp_anchor_presenter_metrics(presenter).worker_pending_jobs&&GetTickCount64()<until);
  CHECK(!pp_anchor_presenter_metrics(presenter).worker_pending_jobs);RECT ref=rectangle(reference_id);
  CHECK(ref.left>=screen.x&&ref.top>=screen.y&&ref.right<=screen.x+screen.w&&ref.bottom<=screen.y+screen.h);
  unsigned char *reference=pp_test_desktop_capture(ref,true);CHECK(reference);
  size_t colored=0;for(size_t i=0;i<(size_t)(ref.right-ref.left)*(ref.bottom-ref.top);i++)if(reference[i*4]<240||reference[i*4+1]<240||reference[i*4+2]<240)colored++;
  CHECK(colored>100);
  /* Keep the independent reference's shared source alive while hiding its
   * window, so a large hand's settled endpoint is ready for the first draw. */
  pp_surface_hide(pp_sprites_find(presenter,reference_id)->surface);
  PPVec2 edge=home;
  if(side==0)edge.x=screen.x+2;if(side==1)edge.y=screen.y+2;
  if(side==2)edge.x=screen.x+screen.w-3;if(side==3)edge.y=screen.y+screen.h-3;
  uint64_t id=spawn(edge,size);RECT first=rectangle(id);
  CHECK(side==0?first.left<screen.x:side==1?first.top<screen.y:side==2?first.right>screen.x+screen.w:first.bottom>screen.y+screen.h);
  printf("First offscreen draw side=%d bounds=%ld,%ld,%ld,%ld\n",side,first.left,first.top,first.right,first.bottom);
  PPRendererMetrics before=pp_anchor_presenter_renderer_metrics(presenter);uint64_t builds=pp_assets_source_metrics(assets).raster_builds;
  CHECK(SetCursorPos((int)edge.x,(int)edge.y));mouse(MOUSEEVENTF_LEFTDOWN,0);CHECK(downs[id]>0);
  /* The native captured input and presenter drag handlers move the subject.
   * Its first draw was clipped; no rotation/resize/content edit follows. */
  for(int step=1;step<=4;step++) {
    move((int)round(edge.x+(home.x-edge.x)*step/4),(int)round(edge.y+(home.y-edge.y)*step/4));
    compare(id,reference,"during captured return");
  }
  mouse(MOUSEEVENTF_LEFTUP,0);CHECK(ups[id]>0&&moves[id]>0);compare(id,reference,"after release");
  if(size==1536) {
    CHECK(displays[0].drawable_pixels_per_dip==1.5);
    CHECK(SetCursorPos((int)home.x,(int)home.y));mouse(MOUSEEVENTF_LEFTDOWN,0);measuring_id=id;measuring_exposure=false;
    for(int i=0;i<24;i++)move((int)home.x+(i%2?8:0),(int)home.y);
    move((int)home.x,(int)home.y);
    measuring_exposure=true;
    for(int i=0;i<24;i++){measuring_id=0;move((int)edge.x,(int)edge.y);measuring_id=id;move((int)home.x,(int)home.y);}
    measuring_id=0;mouse(MOUSEEVENTF_LEFTUP,0);compare(id,reference,"warm exposure returns");
  }
  for(int excursion=0;excursion<3;excursion++) {
    PPVec2 outside=edge;
    if(excursion==2){if(side==0)outside.x-=ref.right-ref.left;if(side==1)outside.y-=ref.bottom-ref.top;if(side==2)outside.x+=ref.right-ref.left;if(side==3)outside.y+=ref.bottom-ref.top;}
    set_position(id,outside);set_position(id,home);compare(id,reference,excursion==2?"complete offscreen return":"repeated partial return");
  }
  PPRendererMetrics after=pp_anchor_presenter_renderer_metrics(presenter);
  CHECK(after.source_uploads==before.source_uploads&&after.transform_draws==before.transform_draws&&after.readbacks==before.readbacks);
  CHECK(pp_assets_source_metrics(assets).raster_builds==builds);CHECK(GetForegroundWindow()==(HWND)receiver->window);
  remove_sprite(id);remove_sprite(reference_id);free(reference);
}
static int order_ms(const void *a,const void *b){double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
static void report_ms(const char *name,double *values,unsigned count){
  CHECK(count>=20);qsort(values,count,sizeof(*values),order_ms);double p95=values[(count*95+99)/100-1],max=values[count-1];
  printf("WARM %s dip=1536 density=1.5 samples=%u complete_p95_ms=%.3f complete_max_ms=%.3f\n",name,count,p95,max);CHECK(p95<=16.7);
}
static void ownership(void){
  PPRect screen=displays[0].bounds;uint64_t id=spawn((PPVec2){screen.x+screen.w*.5,screen.y+screen.h*.5},96);
  PPSprite *s=pp_sprites_find(presenter,id);size_t output=pp_renderer_output_bytes(s->retained.renderer);
  CHECK(output>0&&s->retained.output_bytes==output);
  size_t charged=pp_anchor_presenter_gpu_resources(presenter).retained_bytes;CHECK(charged==s->retained.gpu_bytes+output);
  PPSourcePresentation source={.content_id=s->retained.content_id,.source_revision=s->retained.source_revision,.layout=s->retained.layout,.source_rect=s->retained.source_rect};
  PPPose bigger=s->pose;bigger.longest_dip=384;RECT old=rectangle(id);presenter->gpu.limit_bytes=charged;
  CHECK(!pp_retained_present(presenter,s->surface,&s->retained,source,bigger,displays,&error)&&error.code==PP_ERROR_RESOURCE_LIMIT);
  RECT unchanged=rectangle(id);CHECK(!memcmp(&old,&unchanged,sizeof(old))&&pp_renderer_output_bytes(s->retained.renderer)==output&&pp_anchor_presenter_gpu_resources(presenter).retained_bytes==charged);
  presenter->gpu.limit_bytes=PP_RETAINED_GPU_LIMIT;API(pp_retained_present(presenter,s->surface,&s->retained,source,bigger,displays,&error));
  size_t replacement=pp_renderer_output_bytes(s->retained.renderer);CHECK(replacement>output);
  CHECK(pp_anchor_presenter_gpu_resources(presenter).retained_bytes==s->retained.gpu_bytes+replacement);
  CHECK(pp_anchor_presenter_gpu_resources(presenter).peak_bytes>=s->retained.gpu_bytes+output+replacement);
  pp_sprite_hide(presenter,s);CHECK(!pp_renderer_output_bytes(s->retained.renderer)&&!s->retained.output_bytes);
  CHECK(pp_anchor_presenter_gpu_resources(presenter).retained_bytes==s->retained.gpu_bytes);
  API(pp_retained_present(presenter,s->surface,&s->retained,source,s->pose,displays,&error));
  CHECK(pp_renderer_output_bytes(s->retained.renderer)==output);
  API(pp_renderer_complete(s->retained.renderer,&error));glEnable(0);
  PPPose edge=s->pose;edge.center.x=screen.x+2;API(pp_retained_present(presenter,s->surface,&s->retained,source,edge,displays,&error));
  CHECK(!pp_retained_present(presenter,s->surface,&s->retained,source,s->pose,displays,&error));
  CHECK(error.code==PP_ERROR_RENDER&&!s->retained.output_bytes&&!IsWindowVisible(native(s->surface)));
  remove_sprite(id);CHECK(!pp_anchor_presenter_gpu_resources(presenter).retained_bytes);
  id=spawn((PPVec2){screen.x+screen.w*.5,screen.y+screen.h*.5},96);s=pp_sprites_find(presenter,id);
  API(pp_renderer_complete(s->retained.renderer,&error));
  PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC attachment=(PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC)SDL_GL_GetProcAddress("glGetFramebufferAttachmentParameteriv");CHECK(attachment);
  GLint name=0;attachment(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&name);CHECK(name>0&&glIsTexture((GLuint)name));
  CHECK(DestroyWindow(native(s->surface)));remove_sprite(id);
  API(pp_renderer_complete(presenter->texture_owner,&error));CHECK(!glIsTexture((GLuint)name));
  CHECK(!pp_anchor_presenter_gpu_resources(presenter).retained_bytes);
  puts("PASS real GL output ownership, candidate overlap/rejection, hide/recreate and failed exposure cleanup");
}
int main(int argc,char **argv){
  CHECK(argc==4||argc==5);setvbuf(stdout,NULL,_IONBF,0);old_foreground=GetForegroundWindow();CHECK(GetCursorPos(&old_cursor));atexit(cleanup);SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  SetThreadExecutionState(ES_CONTINUOUS|ES_DISPLAY_REQUIRED|ES_SYSTEM_REQUIRED);
  char name[80];snprintf(name,sizeof(name),"Local\\PointyExposureReceiver-%lu",GetCurrentProcessId());mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(ReceiverState),name);CHECK(mapping);
  receiver=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(*receiver));CHECK(receiver);char command[2048];snprintf(command,sizeof(command),"\"%s\" %s",argv[1],name);STARTUPINFOA startup={sizeof(startup)};
  receiver_job=CreateJobObjectW(NULL,NULL);CHECK(receiver_job);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  CHECK(SetInformationJobObject(receiver_job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)));
  CHECK(CreateProcessA(NULL,command,NULL,NULL,FALSE,CREATE_SUSPENDED,NULL,NULL,&startup,&child));
  CHECK(AssignProcessToJobObject(receiver_job,child.hProcess));CHECK(ResumeThread(child.hThread)!=(DWORD)-1);
  ULONGLONG until=GetTickCount64()+4000;while(!receiver->window&&GetTickCount64()<until)Sleep(10);CHECK(receiver->window);
  API(pp_assets_create(&assets,&error));API(pp_platform_displays(&displays,&display_count,&error));CHECK(display_count>0);
  bool only_ownership=!strcmp(argv[3],"--ownership");
  if(!only_ownership)CHECK(pp_test_desktop_capture_start((RECT){(LONG)displays[0].bounds.x,(LONG)displays[0].bounds.y,(LONG)(displays[0].bounds.x+displays[0].bounds.w),(LONG)(displays[0].bounds.y+displays[0].bounds.h)}));
  PPRect screen=displays[0].bounds;SetWindowLongPtrW((HWND)receiver->window,GWL_STYLE,WS_POPUP|WS_VISIBLE);
  CHECK(SetWindowPos((HWND)receiver->window,HWND_TOP,(int)screen.x,(int)screen.y,(int)screen.w,(int)screen.h,SWP_FRAMECHANGED|SWP_SHOWWINDOW));
  do{SetForegroundWindow((HWND)receiver->window);pump();}while(GetForegroundWindow()!=(HWND)receiver->window&&GetTickCount64()<until);CHECK(GetForegroundWindow()==(HWND)receiver->window);
  if(!strcmp(argv[2],"hand"))API(pp_project_default(assets,&project,&error));
  else {
    API(pp_project_create(assets,&project,&error));char path[1024];snprintf(path,sizeof(path),"%s/retained-blue.png",PP_MEDIA_FIXTURES);size_t count;void *bytes=SDL_LoadFile(path,&count);CHECK(bytes);
    PPItem item={.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=1};API(pp_asset_import(assets,(PPBytes){bytes,count},PP_ASSET_PNG,&item.image_asset,&error));SDL_free(bytes);API(pp_project_add_item(project,PP_ANCHOR_TOP_LEFT,&item,NULL,&error));
  }
  API(pp_scene_create(&scene,&error));API(pp_renderer_create(&renderer,&error));API(pp_anchor_presenter_create_shared(&presenter,renderer,project,scene,&error));pump();
  if(only_ownership){ownership();return 0;}
  int side=atoi(argv[3]);CHECK(side>=0&&side<4);exercise(side,96);
  if(argc==5){CHECK(!strcmp(argv[4],"--large"));exercise(side,1536);report_ms("ordinary position",ordinary_ms,ordinary_count);report_ms("exposure including GPU completion",exposure_ms,exposure_count);printf("INPUT tagged_hooks=%u arrival_max_ms=%u\n",pp_test_exposure_input_count(),pp_test_exposure_input_wait());CHECK(pp_test_exposure_input_count()>40&&pp_test_exposure_input_wait()<=50);}
  pp_anchor_presenter_destroy(presenter);presenter=NULL;
  printf("PASS %s first-offscreen presentation and captured return at edge %d\n",argv[2],side);return 0;
}
