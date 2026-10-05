#undef SDL_GL_CreateContext
#undef pp_surface_present
#undef pp_platform_displays
#include "../../src/app/lifecycle.h"
#include "../../src/app/tray.h"
#include "../../src/app/runtime_internal.h"
#include "../../src/app/presenter_internal.h"
#include "../../src/ui/settings_internal.h"
#include "../test.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <time.h>
static bool fail_context;
static unsigned long long presentations;
static bool offscreen;
bool pp_benchmark_displays(PPDisplay **out,size_t *count,PPError *error) {
  bool ok=pp_platform_displays(out,count,error);
  /* Keep the real display-query cost; translate only presentation geometry. */
  if(ok && offscreen) for(size_t i=0;i<*count;i++) { (*out)[i].bounds.x-=20000; (*out)[i].bounds.y-=20000; }
  return ok;
}
SDL_GLContext pp_test_GL_CreateContext(SDL_Window *window) {
  if(fail_context) { SDL_SetError("Injected GL context failure after window creation"); return NULL; }
  return SDL_GL_CreateContext(window);
}
bool pp_counted_surface_present(PPSurface *surface,const PPFrame *frame,PPRect bounds,PPError *error) {
  bool ok=pp_surface_present(surface,frame,bounds,error);
  if(ok) presentations++;
  return ok;
}
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
static HWND tray_window;
static BOOL CALLBACK owned_tray(HWND window,LPARAM unused) {
  (void)unused; wchar_t name[64]; GetClassNameW(window,name,64);
  if(!wcscmp(name,L"SDL_TRAY")) tray_window=window;
  return TRUE;
}
static void test_shell_reconstruction(void) {
  EnumThreadWindows(GetCurrentThreadId(),owned_tray,0); CHECK(tray_window);
  NOTIFYICONIDENTIFIER identity={0}; identity.cbSize=sizeof(identity); identity.hWnd=tray_window;
  RECT bounds; bool found=false;
  for(UINT i=1;i<256;i++) { identity.uID=i; if(SUCCEEDED(Shell_NotifyIconGetRect(&identity,&bounds))) { found=true; break; } }
  CHECK(found);
  NOTIFYICONDATAW icon={0}; icon.cbSize=sizeof(icon); icon.hWnd=tray_window; icon.uID=identity.uID;
  CHECK(Shell_NotifyIconW(NIM_DELETE,&icon));
  CHECK(FAILED(Shell_NotifyIconGetRect(&identity,&bounds)));
  SendMessageW(tray_window,RegisterWindowMessageW(L"TaskbarCreated"),0,0);
  CHECK(SUCCEEDED(Shell_NotifyIconGetRect(&identity,&bounds)));
  puts("PASS owned tray icon delete + TaskbarCreated restores shell icon; Explorer untouched");
}
#endif
static void test_lifecycle(void) {
  PPError error = {0}; PPLifecycle life = {0};
  CHECK(pp_lifecycle_start(&life, &(PPAppOptions){.preferences_directory=""}, &error));
  CHECK(life.app && life.runtime && life.tray && !life.settings);
#ifdef _WIN32
  test_shell_reconstruction();
#endif
  int entries=0;
  const SDL_TrayEntry **menu=SDL_GetTrayEntries(SDL_GetTrayMenu(life.tray->native),&entries);
  CHECK(entries==2);
  SDL_ClickTrayEntry((SDL_TrayEntry *)menu[1]);
  CHECK(!pp_app_ui(life.app)->quit_requested);
  PPCommandKind queued; CHECK(pp_tray_take(life.tray,&queued) && queued==PP_COMMAND_QUIT);
  CHECK(!pp_app_ui(life.app)->settings_open && !pp_scene_count(pp_app_scene(life.app)));
  CHECK(pp_lifecycle_action(&life, PP_COMMAND_SHOW_SETTINGS, &error));
  CHECK(life.settings);
  life.settings->render_hidden = true;
  PPSettings *same = life.settings;
  strcpy(life.settings->editor.appearance_error,"Invalid retained draft");
  CHECK(pp_app_command(life.app,&(PPCommand){.kind=PP_COMMAND_CLOSE_SETTINGS},&error));
  CHECK(!pp_lifecycle_action(&life, PP_COMMAND_QUIT, &error));
  CHECK(!pp_app_ui(life.app)->quit_requested && life.settings==same);
  life.settings->editor.appearance_error[0]=0;
  CHECK(pp_app_command(life.app,&(PPCommand){.kind=PP_COMMAND_RENAME_PROJECT,.name="Changed"},&error));
  CHECK(pp_lifecycle_action(&life, PP_COMMAND_QUIT, &error));
  CHECK(pp_app_ui(life.app)->transition_pending && !pp_app_ui(life.app)->quit_requested);
  CHECK(pp_app_command(life.app,&(PPCommand){.kind=PP_COMMAND_RESOLVE_TRANSITION,.choice=PP_UNSAVED_CANCEL},&error));
  CHECK(!pp_app_ui(life.app)->quit_requested);
  pp_lifecycle_stop(&life);
  CHECK(!life.app && !life.runtime && !life.settings && !life.tray);
  int count=0; SDL_Window **windows=SDL_GetWindows(&count); CHECK(count==0); SDL_free(windows);
  puts("PASS lifecycle tray-only first launch, retained settings/draft, blocked invalid Quit, dirty Quit cancel and teardown");
}
static void test_gl_failure(void) {
  PPLifecycle life={0}; PPError error={0}; fail_context=true;
  CHECK(!pp_lifecycle_start(&life,&(PPAppOptions){.preferences_directory=""},&error));
  CHECK(error.code==PP_ERROR_RENDER && !life.app && !life.runtime && !life.tray);
  int count=0; SDL_Window **windows=SDL_GetWindows(&count); CHECK(count==0); SDL_free(windows);
#ifdef _WIN32
  CHECK(!GetCapture());
#endif
  fail_context=false;
  puts("PASS failure after hidden native GL-window allocation unwinds all lifecycle owners, windows and capture");
}
static void test_last_project(void) {
  char directory[256],path[300],prefs[300];
  snprintf(directory,sizeof(directory),"lifecycle-%llu",(unsigned long long)SDL_GetPerformanceCounter());
  CHECK(SDL_CreateDirectory(directory));
  snprintf(path,sizeof(path),"%s/saved.pointy",directory);
  snprintf(prefs,sizeof(prefs),"%s/preferences.json",directory);
  PPAppOptions options={directory,true}; PPApp *app=NULL; PPError error={0};
  CHECK(pp_app_create(&app,&options,&error));
  CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_RENAME_PROJECT,.name="Portable \xc3\xa9"},&error));
  CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_SAVE_AS,.path=path},&error));
  pp_app_destroy(app);
  CHECK(pp_app_create(&app,&options,&error));
  CHECK(!strcmp(pp_app_name(app),"Portable \xc3\xa9") && !strcmp(pp_app_path(app),path)); pp_app_destroy(app);
  FILE *file=pp_platform_open_file(path,"wb",&error); CHECK(file); fputs("corrupt",file); fclose(file);
  CHECK(pp_app_create(&app,&options,&error)); CHECK(strstr(pp_app_diagnostic(app),"Last project could not open")); pp_app_destroy(app);
  file=pp_platform_open_file(path,"rb",&error); CHECK(file); CHECK(fgetc(file)=='c'); fclose(file);
  CHECK(SDL_RemovePath(path));
  CHECK(pp_app_create(&app,&options,&error)); CHECK(strstr(pp_app_diagnostic(app),"Last project could not open")); pp_app_destroy(app);
  CHECK(SDL_RemovePath(prefs)); CHECK(SDL_RemovePath(directory));
  puts("PASS saved project/name restore; corrupt and missing project fall back without overwriting source");
}
static double monotonic(void) { return (double)SDL_GetPerformanceCounter()/SDL_GetPerformanceFrequency(); }
static double cpu_seconds(void) {
#ifdef _WIN32
  FILETIME created,exited,kernel,user;
  CHECK(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user));
  ULARGE_INTEGER k,u; k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
  u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
  return (k.QuadPart+u.QuadPart)/1e7;
#else
  return (double)clock()/CLOCKS_PER_SEC;
#endif
}
static void benchmark(const char *mode) {
  PPError error={0}; PPLifecycle life={0}; offscreen=true;
  CHECK(pp_lifecycle_start(&life,&(PPAppOptions){.preferences_directory=""},&error));
  PPApp *app=life.app; PPRuntime *runtime=life.runtime;
  PPAnchorPresenter *p=runtime->presenter;
  double initial=monotonic();
  if(strcmp(mode,"empty")) {
    bool gif=!strcmp(mode,"gif"); size_t size;
    void *bytes=SDL_LoadFile(gif?PP_MEDIA_FIXTURES "/changing-alpha.gif":PP_MEDIA_FIXTURES "/alpha.png",&size);
    CHECK(bytes);
    PPItem item={.kind=PP_ITEM_IMAGE,.image_longest_dip=48,.opacity=1};
    CHECK(pp_asset_import(pp_app_assets(app),(PPBytes){bytes,size},gif?PP_ASSET_GIF:PP_ASSET_PNG,&item.image_asset,&error));
    SDL_free(bytes);
    item.id=pp_project_target(pp_app_project(app),PP_ANCHOR_TOP_LEFT)->id;
    CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_EDIT_ITEM,.item_id=item.id,.item=&item},&error));
    for(int i=0;i<10;i++) {
      PPPose pose={{p->displays[0].bounds.x+200+i*100,p->displays[0].bounds.y+200},48,0,1};
      PPSprite *sprite=pp_sprites_create(p,PP_ANCHOR_TOP_LEFT,pose,initial,&error); CHECK(sprite);
      CHECK(pp_scene_set_pose(pp_app_scene(app),sprite->id,pose,&error));
    }
  }
  /* Native surfaces and actual GL uploads are outside the physical desktop.
   * This measures scheduler/raster/native presentation, not visible compositor work. */
  double start=0,cpu=0; unsigned long long count=0,ticks=0;
  while(monotonic()-initial<7) {
    double now=monotonic();
    if(!start && now-initial>=2) { start=now;cpu=cpu_seconds();count=presentations;ticks=0; }
    CHECK(pp_lifecycle_iteration(&life,&error));
    ticks++;
  }
  double elapsed=monotonic()-start, used=cpu_seconds()-cpu;
  unsigned long long renders=presentations-count;
  printf("BENCH %s full_lifecycle_offscreen_native warmup=2s elapsed=%.6f cpu_seconds=%.6f cpu_one_logical_pct=%.3f cpu_machine_pct=%.3f logical=%d ticks=%llu presentations=%llu presentations_per_sec=%.3f\n",
    mode,elapsed,used,100*used/elapsed,100*used/elapsed/SDL_GetNumLogicalCPUCores(),SDL_GetNumLogicalCPUCores(),ticks,renders,renders/elapsed);
  if(strcmp(mode,"gif")) CHECK(renders==0);
  else CHECK(renders>=400 && renders<=600);
  pp_lifecycle_stop(&life); offscreen=false;
}
int main(int argc,char **argv) {
  if(argc==2 && !strcmp(argv[1],"--benchmark")) { benchmark("empty");benchmark("static");benchmark("gif");return 0; }
  test_gl_failure(); test_lifecycle(); test_last_project(); return 0;
}
