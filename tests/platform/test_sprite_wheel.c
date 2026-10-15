/* Desktop Duplication samples on a separate thread. Native SendInput, the
 * runtime, easing and the retained presenter run without a placement pause.
 * Completed GL image moments identify each displayed pose independently of
 * its center. A previous image at a new native origin keeps its old shape,
 * but disagrees with that image's expected desktop center. */
#include "../../src/app/runtime_internal.h"
#include "../../src/app/presenter_internal.h"
#include "../../src/render/gl_api.h"
#include "../../src/platform/windows/composition.h"
#include "../test.h"
#include <windows.h>
#include <math.h>
#include <string.h>

#define API(expr) do { bool ok = (expr); if (!ok) fprintf(stderr, "%s\n", error.message); CHECK(ok); } while (0)
#define MAX_SAMPLES 4096
typedef struct Moment { double x, y, angle, extent, mass; } Moment;
typedef struct Observation { Moment image; LONGLONG qpc; unsigned workload; } Observation;
typedef struct Reference { Moment image; PPPose pose; LONGLONG qpc; unsigned workload; RECT native; bool moving,drawn; } Reference;
static PPError error;
static PPApp *app;
static PPRuntime *runtime;
static HWND background;
static RECT capture_bounds;
static PPVec2 center;
static double longest, qpc_frequency;
static HANDLE sampler, sampler_ready, sampler_stop;
static Observation observations[MAX_SAMPLES];
static Reference references[MAX_SAMPLES];
static volatile LONG observation_count, workload;
static unsigned reference_count, wheel_events;
static LONGLONG workload_begin[4], workload_end[4];
static bool defect_control;
static bool shift_control, empty_control;
static double reference_readback_ms;
static unsigned reference_readbacks;
static HANDLE injector;
static INPUT injected;
static volatile LONG accepted;
static uint8_t *completed_pixels;

bool pp_test_desktop_capture_start(RECT rectangle);
unsigned char *pp_test_desktop_capture_poll(RECT rectangle);
LONGLONG pp_test_desktop_present_time(void);

static LONGLONG counter(void) { LARGE_INTEGER v; CHECK(QueryPerformanceCounter(&v)); return v.QuadPart; }
static double now(void) { return counter() / qpc_frequency; }
static HWND native(PPSurface *surface) {
  SDL_Window *window = SDL_GetWindowFromID(pp_surface_window_id(surface)); CHECK(window);
  return SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}
static Moment moment(const uint8_t *pixels, int w, int h, int stride, bool bottom_up, double left, double top) {
  double mass=0, sx=0, sy=0, sxx=0, sxy=0, syy=0;
  for (int row=0;row<h;row++) for (int col=0;col<w;col++) {
    const uint8_t *p=pixels+(size_t)row*stride+col*4;
    double value=p[0]+p[1]+p[2], x=col+.5, y=(bottom_up?h-row-1:row)+.5;
    mass+=value; sx+=value*x; sy+=value*y;
    sxx+=value*x*x; sxy+=value*x*y; syy+=value*y*y;
  }
  if (mass<=0) return (Moment){0};
  double x=sx/mass,y=sy/mass,xx=sxx/mass-x*x,xy=sxy/mass-x*y,yy=syy/mass-y*y;
  return (Moment){left+x,top+y,.5*atan2(2*xy,xx-yy),sqrt(fmax(0,xx+yy)),mass};
}
static DWORD WINAPI sample_desktop(void *unused) {
  (void)unused;
  if (!pp_test_desktop_capture_start(capture_bounds)) { SetEvent(sampler_ready); return 1; }
  SetEvent(sampler_ready);
  int w=capture_bounds.right-capture_bounds.left,h=capture_bounds.bottom-capture_bounds.top;
  while (WaitForSingleObject(sampler_stop,0)==WAIT_TIMEOUT) {
    uint8_t *pixels=pp_test_desktop_capture_poll(capture_bounds);
    if (pixels) {
      if (empty_control) { free(pixels); Sleep(1); continue; }
      LONG index=observation_count;
      if (index>=MAX_SAMPLES) { free(pixels); return 2; }
      if (shift_control&&workload) {
        uint8_t *shifted=calloc((size_t)w*h,4); CHECK(shifted);
        /* Apply a known translation to real captured image bytes. Black
         * margins contain the translated artwork, so this models the original
         * stale-content/native-origin defect without placement callbacks. */
        for (int y=19;y<h;y++) memcpy(shifted+(size_t)(y-19)*w*4+3*4,pixels+(size_t)y*w*4,(size_t)(w-3)*4);
        free(pixels); pixels=shifted;
      }
      observations[index]=(Observation){moment(pixels,w,h,w*4,false,capture_bounds.left,capture_bounds.top),
          pp_test_desktop_present_time(),(unsigned)InterlockedCompareExchange(&workload,0,0)};
      free(pixels);
      InterlockedIncrement(&observation_count);
    }
    Sleep(1);
  }
  return 0;
}
static void cleanup(void) {
  if (sampler_stop) SetEvent(sampler_stop);
  if (injector) {
    /* SendInput may be waiting on this process's low-level hook. Pump while
     * joining before the stack/input storage or runtime can disappear. */
    ULONGLONG deadline=GetTickCount64()+2000;
    while (WaitForSingleObject(injector,0)==WAIT_TIMEOUT&&GetTickCount64()<deadline) {
      PPPlatformEvent event; while (pp_platform_poll(&event)) {} Sleep(1);
    }
    if (WaitForSingleObject(injector,0)!=WAIT_OBJECT_0) ExitProcess(2);
    CloseHandle(injector); injector=NULL;
  }
  if (sampler) { CHECK(WaitForSingleObject(sampler,2000)==WAIT_OBJECT_0); CloseHandle(sampler); sampler=NULL; }
  if (sampler_ready) CloseHandle(sampler_ready);
  if (sampler_stop) CloseHandle(sampler_stop);
  pp_runtime_destroy(runtime); runtime=NULL;
  pp_app_destroy(app); app=NULL;free(completed_pixels);completed_pixels=NULL;
  if (background) DestroyWindow(background);
  INPUT up[2]={{.type=INPUT_KEYBOARD},{.type=INPUT_MOUSE}};
  up[0].ki.wVk=VK_MENU; up[0].ki.dwFlags=KEYEVENTF_KEYUP; up[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
  SendInput(2,up,sizeof(up[0]));
}
/* Diagnostic control only. The ordinary workload never enables this pause.
 * The capture thread observes the original placement/content defect without
 * depending on this callback for its samples or its passing condition. */
void pp_test_sprite_placed(void) { if (defect_control&&workload) Sleep(35); }


static int completed_width,completed_height;
static Moment completed_image(PPSprite *sprite,RECT *bounds) {
  CHECK(completed_pixels);CHECK(GetWindowRect(native(sprite->surface),bounds));
  return moment(completed_pixels,completed_width,completed_height,completed_width*4,true,bounds->left,bounds->top);
}
static void completed_draw(PPRenderer *renderer,int x,int y,int w,int h,bool top_down,void *context) {
  (void)context;
  if(!runtime||!runtime->presenter||runtime->presenter->instance_count!=1)return;
  PPSprite *sprite=runtime->presenter->instances;if(renderer!=sprite->retained.renderer)return;
  LONGLONG start=counter();uint8_t *raw=malloc((size_t)w*h*4);CHECK(raw);
  glReadPixels(x,y,w,h,GL_RGBA,GL_UNSIGNED_BYTE,raw);CHECK(glGetError()==GL_NO_ERROR);
  free(completed_pixels);completed_pixels=malloc((size_t)w*h*4);CHECK(completed_pixels);
  for(int row=0;row<h;row++)memcpy(completed_pixels+(size_t)row*w*4,raw+(size_t)(top_down?h-row-1:row)*w*4,(size_t)w*4);
  free(raw);completed_width=w;completed_height=h;
  reference_readback_ms+=(counter()-start)*1000/qpc_frequency;reference_readbacks++;
  if(!workload)return;
  CHECK(reference_count<MAX_SAMPLES);Reference *r=references+reference_count++;
  *r=(Reference){.pose=sprite->pose,.qpc=start,.workload=(unsigned)workload,.moving=sprite->rotating||sprite->resizing,.drawn=true};
  r->image=completed_image(sprite,&r->native);
  printf("POSE workload=%u t=%.6f center=%.6f,%.6f longest_dip=%.6f angle_deg=%.6f easing=%d pivot=%.6f,%.6f native=%ld,%ld,%ld,%ld GL_center=%.6f,%.6f\n",
      r->workload,r->qpc/qpc_frequency,r->pose.center.x,r->pose.center.y,r->pose.longest_dip,r->pose.angle_deg,
      sprite->rotating||sprite->resizing,sprite->rotate_pivot.x,sprite->rotate_pivot.y,
      r->native.left,r->native.top,r->native.right,r->native.bottom,r->image.x,r->image.y);
}
static void tick(void) {
  PPPlatformEvent event;
  while (pp_platform_poll(&event)) {
    if (event.kind==PP_EVENT_WHEEL) {
      wheel_events++;
      printf("WHEEL workload=%ld t=%.6f delta=%.3f alt=%d pointer=%.3f,%.3f instance=%llu\n",
          workload,event.timestamp,event.wheel_delta,!!(event.modifiers&PP_MOD_ALT),event.desktop_point.x,event.desktop_point.y,(unsigned long long)event.instance_id);
    }
    pp_runtime_event(runtime,&event);
  }
  pp_runtime_tick(runtime,now());CHECK(!runtime->failed);
  if(workload&&completed_pixels&&runtime->presenter->instance_count==1) {
    PPSprite *sprite=runtime->presenter->instances;LONGLONG timestamp=counter();
    if(!sprite->rotating&&!sprite->resizing&&sprite->retained.visible&&reference_count&&
       timestamp-references[reference_count-1].qpc>=qpc_frequency*.05) {
      /* No publication changed these completed pixels. Refresh the retained
       * state's timestamp while it stays visible, so a settled image remains
       * eligible under the same 250 ms desktop freshness gate. These entries
       * never count as a new draw or a distinct easing pose. */
      CHECK(reference_count<MAX_SAMPLES);Reference *r=references+reference_count++;
      *r=(Reference){.pose=sprite->pose,.qpc=timestamp,.workload=(unsigned)workload};
      r->image=completed_image(sprite,&r->native);
      printf("RETAINED_REFERENCE workload=%u t=%.6f GL_center=%.6f,%.6f\n",r->workload,timestamp/qpc_frequency,r->image.x,r->image.y);
    }
  }
}
static void pump(unsigned ms) {
  ULONGLONG end=GetTickCount64()+ms;
  do { tick(); Sleep(1); } while (GetTickCount64()<end);
}
static DWORD WINAPI inject(void *unused) { (void)unused; accepted=(LONG)SendInput(1,&injected,sizeof(injected)); return 0; }
static void send(INPUT input) {
  CHECK(!injector); injected=input; accepted=0; injector=CreateThread(NULL,0,inject,NULL,0,NULL); CHECK(injector);
  ULONGLONG deadline=GetTickCount64()+2000;
  while (WaitForSingleObject(injector,0)==WAIT_TIMEOUT&&GetTickCount64()<deadline) { tick(); Sleep(1); }
  CHECK(WaitForSingleObject(injector,0)==WAIT_OBJECT_0); CloseHandle(injector); injector=NULL; CHECK(accepted==1);
  pump(5);
}
static void alt(bool down) { INPUT input={.type=INPUT_KEYBOARD}; input.ki.wVk=VK_MENU; input.ki.dwFlags=down?0:KEYEVENTF_KEYUP; send(input); }
static void pointer_on_artwork(PPSprite *sprite) {
  RECT bounds; Moment m=completed_image(sprite,&bounds);
  /* Find a real visible pixel away from the artwork center, so mouse-position
   * rotation exercises the pivot rather than coinciding with center rotation. */
  int w,h; CHECK(SDL_GetWindowSizeInPixels(SDL_GetWindowFromID(pp_surface_window_id(sprite->surface)),&w,&h));
  uint8_t *pixels=completed_pixels;CHECK(completed_width==w&&completed_height==h);
  POINT point={0}; bool found=false;
  for (int x=w/3;x<w*2/3&&!found;x++) for (int y=0;y<h;y++) {
    if (pixels[((size_t)y*w+x)*4+3]<200) continue;
    point=(POINT){bounds.left+x,bounds.top+h-y-1};
    if (fabs(point.x-m.x)>longest*.05&&WindowFromPoint(point)==native(sprite->surface)) { found=true; break; }
  }
  CHECK(found); CHECK(SetCursorPos(point.x,point.y)); pump(10);
}
static double shape_error(Moment a, Moment b) {
  double angle=remainder(a.angle-b.angle,3.14159265358979323846);
  return fmax(fabs(angle)*b.extent,fabs(a.extent-b.extent));
}
static double displacement(Moment a, Moment b) { return fmax(fabs(a.x-b.x),fabs(a.y-b.y)); }
static bool analyze(void) {
  unsigned meaningful[4]={0}, moving[4]={0}, distinct[4]={0}, bad[4]={0}, unmatched[4]={0}, blank[4]={0}; double maxima[4]={0};
  bool seen[MAX_SAMPLES]={0};
  for (LONG i=0;i<observation_count;i++) {
    Observation o=observations[i]; o.workload=0;
    /* Acquisition may finish after a workload boundary. Attribute the actual
     * desktop-image QPC timestamp, never the collector thread's current tag. */
    for (unsigned k=0;k<4;k++) if (o.qpc>=workload_begin[k]&&o.qpc<=workload_end[k]) o.workload=k+1;
    if (!o.workload||!o.qpc) continue;
    if (o.image.mass<=0) { blank[o.workload-1]++; continue; }
    unsigned selected=MAX_SAMPLES; double best=HUGE_VAL;
    for (unsigned j=0;j<reference_count;j++) {
      Reference r=references[j]; if (r.workload!=o.workload||r.qpc>o.qpc||o.qpc-r.qpc>qpc_frequency*.25) continue;
      double shape=shape_error(o.image,r.image);
      if (shape<=best) { best=shape; selected=j; }
    }
    unsigned k=o.workload-1;
    if (selected==MAX_SAMPLES||best>.15) {
      unmatched[k]++;
      printf("UNMATCHED workload=%u t=%.6f shape_error=%.6fpx center=%.6f,%.6f angle=%.6f extent=%.6f mass=%.0f reference_angle=%.6f reference_extent=%.6f reference_mass=%.0f\n",
          o.workload,o.qpc/qpc_frequency,best,o.image.x,o.image.y,o.image.angle,o.image.extent,o.image.mass,
          selected==MAX_SAMPLES?0:references[selected].image.angle,selected==MAX_SAMPLES?0:references[selected].image.extent,
          selected==MAX_SAMPLES?0:references[selected].image.mass);
      continue;
    }
    double drift=displacement(o.image,references[selected].image);
    meaningful[k]++; maxima[k]=fmax(maxima[k],drift); if (drift>=.1) bad[k]++;
    if (references[selected].drawn&&references[selected].moving) { moving[k]++; if (!seen[selected]) distinct[k]++; }
    seen[selected]=true;
    printf("DESKTOP workload=%u qpc_s=%.6f reference_s=%.6f shape_error=%.6fpx center=%.6f,%.6f expected=%.6f,%.6f displacement=%.6fpx\n",
        o.workload,o.qpc/qpc_frequency,references[selected].qpc/qpc_frequency,best,o.image.x,o.image.y,
        references[selected].image.x,references[selected].image.y,drift);
  }
  bool meaningful_all=true, complete_all=true, no_displacement=true;
  for (unsigned k=0;k<4;k++) {
    unsigned poses=0; for (unsigned j=0;j<reference_count;j++) if (references[j].workload==k+1&&references[j].drawn) poses++;
    printf("RESULT workload=%u completed_GL_poses=%u desktop_matched=%u desktop_active_easing=%u distinct_easing_poses=%u desktop_unmatched=%u blank=%u displaced=%u maximum=%.6fpx\n",
        k+1,poses,meaningful[k],moving[k],distinct[k],unmatched[k],blank[k],bad[k],maxima[k]);
    meaningful_all=meaningful_all&&meaningful[k]>=10&&distinct[k]>=10&&poses>=10;
    complete_all=complete_all&&!unmatched[k]&&!blank[k];
    no_displacement=no_displacement&&!bad[k];
  }
  printf("RESULT ordinary=%d wheel_events=%u references=%u actual_desktop_samples=%ld meaningful=%d complete_desktop=%d no_displacement=%d\n",
      !defect_control,wheel_events,reference_count,observation_count,meaningful_all,complete_all,no_displacement);
  printf("MEASUREMENT reference_readbacks=%u reference_readback_total_ms=%.3f mean_ms=%.3f collector=independent-desktop-thread controls=placement:%d,shift:%d,empty:%d\n",
      reference_readbacks,reference_readback_ms,reference_readbacks?reference_readback_ms/reference_readbacks:0,defect_control,shift_control,empty_control);
  return wheel_events==24&&meaningful_all&&complete_all&&no_displacement;
}
static void setup_desktop(void) {
  POINT pointer; CHECK(GetCursorPos(&pointer)); MONITORINFO monitor={.cbSize=sizeof(monitor)};
  CHECK(GetMonitorInfoW(MonitorFromPoint(pointer,MONITOR_DEFAULTTOPRIMARY),&monitor));
  int width=monitor.rcMonitor.right-monitor.rcMonitor.left,height=monitor.rcMonitor.bottom-monitor.rcMonitor.top;
  int w=min(2800,width-40),h=min(1100,height-40); CHECK(w>=800&&h>=500);
  center=(PPVec2){monitor.rcMonitor.left+width*.5+.25,monitor.rcMonitor.top+height*.5+.75};
  capture_bounds=(RECT){(LONG)(center.x-w*.5),(LONG)(center.y-h*.5),(LONG)(center.x-w*.5)+w,(LONG)(center.y-h*.5)+h};
  PPDisplay *displays=NULL; size_t count=0; API(pp_platform_displays(&displays,&count,&error));
  const PPDisplay *display=pp_display_at(displays,count,center); CHECK(display);
  longest=fmin(1536,w*.85/display->desktop_units_per_dip);
  printf("ENV configuration=%s presenter=shared-retained monitor=%ld,%ld,%ld,%ld capture=%ld,%ld,%ld,%ld actual_desktop_density=%.3f actual_drawable_density=%.3f simulated_density=none longest_dip=%.3f forced_placement_pause_ms=%d\n",
      PP_WHEEL_CONFIGURATION,monitor.rcMonitor.left,monitor.rcMonitor.top,monitor.rcMonitor.right,monitor.rcMonitor.bottom,
      capture_bounds.left,capture_bounds.top,capture_bounds.right,capture_bounds.bottom,
      display->desktop_units_per_dip,display->drawable_pixels_per_dip,longest,defect_control?35:0);
  pp_displays_free(displays);
  WNDCLASSW cls={.lpfnWndProc=DefWindowProcW,.hInstance=GetModuleHandle(NULL),.lpszClassName=L"YoinkWheelCaptureBackground",
      .hbrBackground=GetStockObject(BLACK_BRUSH)}; CHECK(RegisterClassW(&cls));
  /* Own the foreground during injected Alt gestures. Otherwise the unrelated
   * application's menu can open on Alt-up and contaminate desktop captures. */
  background=CreateWindowExW(WS_EX_TOPMOST,cls.lpszClassName,L"Yoink wheel desktop test",WS_POPUP|WS_VISIBLE,
      capture_bounds.left,capture_bounds.top,w,h,NULL,NULL,cls.hInstance,NULL); CHECK(background); CHECK(UpdateWindow(background));
  SetForegroundWindow(background);
  if (GetForegroundWindow()!=background) {
    /* Windows can deny programmatic activation. Activate only the observed
     * owned background with an ordinary complete native click before hooks. */
    POINT point={capture_bounds.left+10,capture_bounds.top+10};
    CHECK(SetCursorPos(point.x,point.y)); CHECK(WindowFromPoint(point)==background);
    INPUT click[2]={{.type=INPUT_MOUSE},{.type=INPUT_MOUSE}};
    click[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN; click[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    CHECK(SendInput(2,click,sizeof(click[0]))==2);
    ULONGLONG deadline=GetTickCount64()+1000;
    do {
      MSG message; while (PeekMessageW(&message,NULL,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
      if (GetForegroundWindow()==background) break; Sleep(1);
    } while (GetTickCount64()<deadline);
  }
  CHECK(GetForegroundWindow()==background);
}
static void workload_run(unsigned kind) {
  PPItem item={.kind=PP_ITEM_TEXT,.opacity=1,.angle_deg=17.25,.rotation_center=PP_ROTATION_MOUSE_POSITION};
  API(pp_text_default_style(pp_app_assets(app),&item.text_style,&error)); item.text_style.color[0]=255;item.text_style.color[1]=255;item.text_style.color[2]=255;
  item.text=kind<2?"MMMM MMMM MMMM MMMM":"\xf0\x9f\x91\x89\xf0\x9f\x91\x89\xf0\x9f\x91\x89\xf0\x9f\x91\x89\xf0\x9f\x91\x89\xf0\x9f\x91\x89\xf0\x9f\x91\x89\xf0\x9f\x91\x89";
  uint64_t id=0;
  API(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_ADD_ITEM,.anchor=PP_ANCHOR_TOP_LEFT,.item=&item},&error));
  PPItemSnapshot *snapshot=pp_project_snapshot(pp_app_project(app),pp_app_ui(app)->selected_item); CHECK(snapshot);
  PPSprite *created=pp_sprites_create_snapshot(runtime->presenter,snapshot,PP_ANCHOR_TOP_LEFT,(PPPose){center,longest,17.25,1},now(),&error);
  CHECK(created); id=created->id;
  pp_item_snapshot_release(snapshot);
  API(pp_scene_set_pose(pp_app_scene(app),id,(PPPose){center,longest,17.25,1},&error));
  pump(500); CHECK(runtime->presenter->instance_count==1);
  PPSprite *sprite=runtime->presenter->instances; CHECK(sprite->retained.valid);
  SDL_Window *window=SDL_GetWindowFromID(pp_surface_window_id(sprite->surface));
  HWND native=SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,NULL);
  printf("PRESENTATION %s\n",pp_composition_description(GetPropW(native,PP_COMPOSITION_PROPERTY)));
  PPVec2 layout=pp_item_snapshot_size(pp_scene_get(pp_app_scene(app),id)->snapshot); CHECK(layout.x/layout.y>5);
  printf("WORKLOAD %u content=%s operation=%s text=\"%s\" canonical_layout=%.3f,%.3f aspect=%.3f initial_center=%.3f,%.3f initial_dip=%.3f initial_angle=17.25 pivot=mouse-position wheels=6 direction=alternating cadence_ms=200 easing_seconds=%.3f\n",
      kind+1,kind<2?"text":"emoji",kind%2?"zoom":"rotation",item.text,layout.x,layout.y,layout.x/layout.y,
      center.x,center.y,longest,pp_project_settings(pp_app_project(app))->easing_seconds);
  printf("GPU vendor=%s renderer=%s version=%s\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
  CHECK(reference_count<MAX_SAMPLES); Reference *r=references+reference_count++;
  *r=(Reference){.pose=sprite->pose,.qpc=counter(),.workload=kind+1}; r->image=completed_image(sprite,&r->native);
  workload_begin[kind]=counter(); InterlockedExchange(&workload,(LONG)kind+1);
  if (kind%2) alt(true);
  for (unsigned step=0;step<6;step++) {
    pointer_on_artwork(sprite);
    INPUT input={.type=INPUT_MOUSE}; input.mi.dwFlags=MOUSEEVENTF_WHEEL; input.mi.mouseData=(DWORD)(step%2?-120:120);
    send(input);
    printf("REQUEST workload=%u step=%u t=%.6f target_longest_dip=%.6f target_angle_deg=%.6f pivot=%.6f,%.6f\n",
        kind+1,step+1,now(),sprite->interaction.target_pose.longest_dip,sprite->interaction.target_pose.angle_deg,sprite->rotate_pivot.x,sprite->rotate_pivot.y);
    pump(200);
  }
  if (kind%2) alt(false);
  pump(100); workload_end[kind]=counter(); InterlockedExchange(&workload,0);
  pp_scene_delete(pp_app_scene(app),id); pump(50);
}
int main(int argc,char **argv) {
  /* Per-character redirected writes can stall the owner thread long enough
   * to skip easing poses. Flush this bounded probe's log at normal exit. */
  static char output_buffer[1024*1024];setvbuf(stdout,output_buffer,_IOFBF,sizeof(output_buffer));
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  defect_control=argc==2&&!strcmp(argv[1],"--defect-control");
  shift_control=argc==2&&!strcmp(argv[1],"--shift-control");
  empty_control=argc==2&&!strcmp(argv[1],"--empty-control");
  CHECK(argc==1||defect_control||shift_control||empty_control);
  LARGE_INTEGER frequency; CHECK(QueryPerformanceFrequency(&frequency)); qpc_frequency=(double)frequency.QuadPart;
  atexit(cleanup);pp_renderer_diagnostic_observer(completed_draw,NULL); setup_desktop();
  API(pp_app_create(&app,&(PPAppOptions){.preferences_directory=""},&error));
  for (unsigned a=0;a<8;a++) API(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_SET_ENABLED,.anchor=(PPAnchor)a,.enabled=false},&error));
  PPProjectSettings settings=*pp_project_settings(pp_app_project(app)); settings.rotation_step_deg=1;settings.resize_factor=1.01;settings.easing_seconds=.12;
  API(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_SET_INTERACTION,.interaction=settings},&error));
  API(pp_runtime_create(&runtime,app,&error)); pump(10);
  sampler_ready=CreateEvent(NULL,TRUE,FALSE,NULL); sampler_stop=CreateEvent(NULL,TRUE,FALSE,NULL); CHECK(sampler_ready&&sampler_stop);
  sampler=CreateThread(NULL,0,sample_desktop,NULL,0,NULL); CHECK(sampler); CHECK(WaitForSingleObject(sampler_ready,2000)==WAIT_OBJECT_0);
  for (unsigned kind=0;kind<4;kind++) workload_run(kind);
  SetEvent(sampler_stop); CHECK(WaitForSingleObject(sampler,2000)==WAIT_OBJECT_0); DWORD code=1; CHECK(GetExitCodeThread(sampler,&code)&&code==0);
  CHECK(analyze());
  return 0;
}
