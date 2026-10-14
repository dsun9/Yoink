#undef SDL_GL_CreateContext
#undef SDL_GL_MakeCurrent
#undef SDL_ShowMessageBox
#undef pp_platform_file_dialog
#undef pp_platform_poll_dialogs
#undef pp_surface_present
#undef pp_platform_displays
#undef SDL_GetPrefPath
#include "../../src/app/lifecycle.h"
#include "../../src/app/tray.h"
#include "../../src/app/runtime_internal.h"
#include "../../src/app/presenter_internal.h"
#include "../../src/ui/settings_internal.h"
#include "../test.h"
#include "pointy/project_store.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <time.h>
static bool fail_context;
static unsigned long long presentations;
static bool offscreen;
static SDL_Window *failed_settings_window;
static int recovery_choice=-1, recovery_boxes;
static bool recovery_save_expected;
static PPDialogCallback recovery_file_callback;
static void *recovery_file_userdata;
static const char *recovery_file_result;
static const char *legacy_preferences_directory;
char *pp_test_GetPrefPath(const char *organization, const char *application) {
  if (!legacy_preferences_directory)
    return SDL_GetPrefPath(organization, application);
  /* Replace only the OS directory lookup, leaving the legacy file reader real. */
  CHECK(!strcmp(organization, "PointyPointer"));
  CHECK(!strcmp(application, "PointyPointer"));
  return SDL_strdup(legacy_preferences_directory);
}
void pp_test_file_dialog(PPDialogKind kind, const char *suggested,
                         PPDialogCallback callback, void *userdata) {
  (void)suggested;
  CHECK(kind==PP_DIALOG_SAVE_PROJECT && !recovery_file_callback);
  recovery_file_callback=callback; recovery_file_userdata=userdata;
}
void pp_test_poll_dialogs(void) {
  pp_platform_poll_dialogs();
  if(recovery_file_callback) {
    PPDialogCallback callback=recovery_file_callback;
    recovery_file_callback=NULL;
    callback(recovery_file_userdata,recovery_file_result,NULL);
  }
}
bool pp_test_ShowMessageBox(const SDL_MessageBoxData *box, int *choice) {
  CHECK(recovery_choice>=0);
  CHECK(!strcmp(box->title,"Yoink Settings recovery"));
  CHECK(strstr(box->message,"Injected Settings context failure"));
  CHECK(strstr(box->message,"AND pending editor drafts"));
  CHECK(box->numbuttons == (recovery_save_expected ? 4 : 3));
  recovery_boxes++;
  *choice=recovery_choice;
  if(*choice==1) failed_settings_window=NULL;
  return true;
}
bool pp_test_GL_MakeCurrent(SDL_Window *window, SDL_GLContext context) {
  if (failed_settings_window && window == failed_settings_window) {
    SDL_SetError("Injected Settings context failure");
    return false;
  }
  return SDL_GL_MakeCurrent(window, context);
}
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
static void test_failed_settings(void) {
  for (int dirty=0; dirty<2; dirty++) {
    PPError error={0}; PPLifecycle life={0};
    CHECK(pp_lifecycle_start(&life, &(PPAppOptions){.preferences_directory=""}, &error));
    CHECK(pp_lifecycle_action(&life, PP_COMMAND_SHOW_SETTINGS, &error));
    life.settings->render_hidden=true;
    pp_settings_draw(life.settings);
    if (dirty)
      CHECK(pp_app_command(life.app, &(PPCommand){.kind=PP_COMMAND_RENAME_PROJECT,.name="Unsaved recovery"}, &error));
    PPSettings *retained=life.settings;
    failed_settings_window=life.settings->backend->window;
    pp_settings_draw(life.settings);
    CHECK(life.settings->failed && strstr(life.settings->status,"Injected Settings"));
    failed_settings_window=NULL;
    CHECK(pp_lifecycle_action(&life, PP_COMMAND_SHOW_SETTINGS, &error));
    CHECK(life.settings==retained && !life.settings->failed);
    CHECK(pp_lifecycle_action(&life, PP_COMMAND_QUIT, &error));
    CHECK(pp_app_ui(life.app)->transition_pending == (dirty!=0));
    CHECK(pp_app_ui(life.app)->quit_requested == (dirty==0));
    if(dirty) {
      CHECK(pp_app_command(life.app,&(PPCommand){.kind=PP_COMMAND_RESOLVE_TRANSITION,.choice=PP_UNSAVED_CANCEL},&error));
      CHECK(!pp_app_ui(life.app)->quit_requested && pp_app_dirty(life.app));
    }
    pp_lifecycle_stop(&life);
  }
  puts("PASS failed Settings retry retains instance; clean Quit and dirty Cancel reachable");
}
static void settings_key(PPSettings *settings, PPKey key, unsigned modifiers) {
  PPPlatformEvent event = {.kind = PP_EVENT_KEY_DOWN,
      .window_id = pp_settings_window_id(settings), .key = key, .modifiers = modifiers};
  pp_settings_event(settings, &event);
  event.kind = PP_EVENT_KEY_UP; pp_settings_event(settings, &event);
}
static void test_failed_font_disclosure(void) {
  PPError error = {0}; PPLifecycle life = {0};
  CHECK(pp_lifecycle_start(&life, &(PPAppOptions){.preferences_directory=""}, &error));
  CHECK(pp_lifecycle_action(&life, PP_COMMAND_SHOW_SETTINGS, &error));
  PPSettings *retained = life.settings;
  retained->render_hidden = true; pp_settings_draw(retained);
  pp_ui_focus(retained->backend, "font-details");
  settings_key(retained, PP_KEY_ENTER, 0); pp_settings_draw(retained);
  CHECK(retained->editor.font_details_open);
  pp_ui_focus(retained->backend, "font-face"); pp_settings_draw(retained);
  settings_key(retained, PP_KEY_A, PP_MOD_CONTROL); pp_settings_draw(retained);
  for (const char *p = "120"; *p; p++) {
    PPPlatformEvent text = {.kind = PP_EVENT_TEXT,
        .window_id = pp_settings_window_id(retained), .codepoint = (unsigned char)*p};
    pp_settings_event(retained, &text);
  }
  settings_key(retained, PP_KEY_TAB, PP_MOD_SHIFT);
  settings_key(retained, PP_KEY_ENTER, 0);
  failed_settings_window = retained->backend->window;
  pp_settings_draw(retained); CHECK(retained->failed);
  failed_settings_window = NULL;
  CHECK(pp_lifecycle_action(&life, PP_COMMAND_SHOW_SETTINGS, &error));
  CHECK(life.settings == retained && !retained->failed);
  CHECK(!strcmp(retained->editor.face, "120"));
  CHECK(!strcmp(retained->editor.size, "96"));
  CHECK(retained->editor.font_error[0] && pp_settings_invalid(retained));
  CHECK(!retained->editor.font_details_open);
  CHECK(pp_project_item(pp_app_project(life.app), retained->editor.id)->text_style.font_size_dip == 96);
  pp_lifecycle_stop(&life);
  puts("PASS Settings retry retains queued collection edit, Tab focus and disclosure activation.");
}
static void test_persistent_settings_failure(void) {
  for(int dirty=0;dirty<2;dirty++) for(int invalid=0;invalid<2;invalid++) {
    PPError error={0}; PPLifecycle life={0};
    CHECK(pp_lifecycle_start(&life,&(PPAppOptions){.preferences_directory=""},&error));
    CHECK(pp_lifecycle_action(&life,PP_COMMAND_SHOW_SETTINGS,&error));
    PPSettings *retained=life.settings;
    retained->render_hidden=true; pp_settings_draw(retained);
    if(dirty) CHECK(pp_app_command(life.app,&(PPCommand){.kind=PP_COMMAND_RENAME_PROJECT,.name="Keep unsaved"},&error));
    if(invalid) {
      strcpy(retained->editor.size,"0");
      strcpy(retained->editor.appearance_error,"Size is invalid");
    }
    failed_settings_window=retained->backend->window;
    pp_settings_draw(retained); CHECK(retained->failed);
    recovery_save_expected=!invalid; recovery_choice=0; recovery_boxes=0;
    CHECK(!pp_lifecycle_action(&life,PP_COMMAND_QUIT,&error));
    CHECK(recovery_boxes==1 && life.settings==retained && retained->failed);
    CHECK(!pp_app_ui(life.app)->quit_requested && pp_app_dirty(life.app)==(dirty!=0));
    if(invalid) CHECK(!strcmp(retained->editor.size,"0") && pp_settings_invalid(retained));
    recovery_choice=1;
    CHECK(pp_lifecycle_action(&life,PP_COMMAND_SHOW_SETTINGS,&error));
    CHECK(recovery_boxes==2 && !retained->failed);
    CHECK(strstr(retained->status,"Earlier rendering failure"));
    if(invalid) CHECK(!strcmp(retained->editor.size,"0") && pp_settings_invalid(retained));
    /* Fail again with unconsumed valid text queued: Save must not bypass it. */
    pp_ui_focus(retained->backend,"project-name");
    pp_settings_draw(retained);
    PPPlatformEvent text={.kind=PP_EVENT_TEXT,.window_id=pp_settings_window_id(retained),.codepoint='X'};
    pp_settings_event(retained,&text);
    CHECK(retained->backend->input_started);
    failed_settings_window=retained->backend->window; pp_settings_draw(retained);
    recovery_save_expected=false; recovery_choice=0;
    CHECK(!pp_lifecycle_action(&life,PP_COMMAND_QUIT,&error));
    CHECK(retained->backend->input_started && retained->backend->context.input.keyboard.text_len>0);
    CHECK(!pp_app_ui(life.app)->quit_requested);
    recovery_choice=2;
    CHECK(pp_lifecycle_action(&life,PP_COMMAND_QUIT,&error));
    CHECK(pp_app_ui(life.app)->quit_requested);
    failed_settings_window=NULL; recovery_choice=-1;
    pp_lifecycle_stop(&life);
  }
  puts("PASS persistent Settings failure: clean/dirty, invalid/queued drafts, Cancel/Retry retention and explicit discard Quit");
}
static void test_failed_settings_save(void) {
  PPError error={0}; PPLifecycle life={0}; char path[256];
  snprintf(path,sizeof(path),"recovery-save-%llu.pointy",(unsigned long long)SDL_GetPerformanceCounter());
  CHECK(pp_lifecycle_start(&life,&(PPAppOptions){.preferences_directory=""},&error));
  CHECK(pp_app_command(life.app,&(PPCommand){.kind=PP_COMMAND_SAVE_AS,.path=path},&error));
  CHECK(pp_lifecycle_action(&life,PP_COMMAND_SHOW_SETTINGS,&error));
  life.settings->render_hidden=true; pp_settings_draw(life.settings);
  CHECK(pp_app_command(life.app,&(PPCommand){.kind=PP_COMMAND_RENAME_PROJECT,.name="Saved by recovery"},&error));
  failed_settings_window=life.settings->backend->window; pp_settings_draw(life.settings);
  recovery_save_expected=true; recovery_choice=3;
  CHECK(pp_lifecycle_action(&life,PP_COMMAND_QUIT,&error));
  CHECK(pp_app_ui(life.app)->quit_requested && !pp_app_dirty(life.app));
  PPProject *saved=NULL;
  CHECK(pp_project_load(path,pp_app_assets(life.app),&saved,&error));
  CHECK(!strcmp(pp_project_name(saved),"Saved by recovery")); pp_project_destroy(saved);
  failed_settings_window=NULL; recovery_choice=-1; pp_lifecycle_stop(&life);
  CHECK(SDL_RemovePath(path));
  puts("PASS persistent Settings failure saves validated dirty project before Quit");
}
static void test_failed_settings_save_dialog(void) {
  for(int cancel=0;cancel<2;cancel++) {
    PPError error={0}; PPLifecycle life={0}; char path[256];
    snprintf(path,sizeof(path),"recovery-dialog-%llu.pointy",(unsigned long long)SDL_GetPerformanceCounter());
    CHECK(pp_lifecycle_start(&life,&(PPAppOptions){.preferences_directory=""},&error));
    CHECK(pp_lifecycle_action(&life,PP_COMMAND_SHOW_SETTINGS,&error));
    life.settings->render_hidden=true; pp_settings_draw(life.settings);
    CHECK(pp_app_command(life.app,&(PPCommand){.kind=PP_COMMAND_RENAME_PROJECT,.name="New unsaved project"},&error));
    failed_settings_window=life.settings->backend->window; pp_settings_draw(life.settings);
    recovery_save_expected=true; recovery_choice=3; recovery_file_result=cancel ? NULL : path;
    CHECK(!pp_lifecycle_action(&life,PP_COMMAND_QUIT,&error));
    CHECK(life.settings->dialog_pending && recovery_file_callback);
    CHECK(!pp_app_ui(life.app)->quit_requested);
    /* Even the failed renderer must drain the deferred file-dialog result. */
    pp_settings_draw(life.settings);
    CHECK(!life.settings->dialog_pending && !recovery_file_callback);
    CHECK(pp_app_ui(life.app)->quit_requested == !cancel);
    CHECK(pp_app_dirty(life.app) == (cancel!=0));
    CHECK(!pp_app_ui(life.app)->transition_pending);
    if(!cancel) {
      PPProject *saved=NULL; CHECK(pp_project_load(path,pp_app_assets(life.app),&saved,&error));
      CHECK(!strcmp(pp_project_name(saved),"New unsaved project")); pp_project_destroy(saved);
      CHECK(SDL_RemovePath(path));
    }
    failed_settings_window=NULL; recovery_choice=-1; pp_lifecycle_stop(&life);
  }
  puts("PASS failed Settings drains deferred Save As success/cancel without losing unsaved state");
}
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
static void test_public_identity(void) {
  char directory[256], preferences[300], saved[300];
  snprintf(directory,sizeof(directory),"legacy-preferences-%llu",(unsigned long long)SDL_GetPerformanceCounter());
  CHECK(SDL_CreateDirectory(directory));
  snprintf(preferences,sizeof(preferences),"%s/preferences.json",directory);
  snprintf(saved,sizeof(saved),"%s/renamed-project.pointy",directory);
  FILE *file=pp_platform_open_file(preferences,"wb",NULL); CHECK(file);
  fprintf(file,"{\"last_project\":\"%s\",\"geometry\":[120,140,960,720]}",PP_EXAMPLE_PROJECT);
  CHECK(!fclose(file));
  legacy_preferences_directory=directory;
  PPError error={0}; PPLifecycle life={0};
  CHECK(pp_lifecycle_start(&life,NULL,&error));
  CHECK(!strcmp(pp_app_path(life.app),PP_EXAMPLE_PROJECT));
  CHECK(!strcmp(pp_app_name(life.app),"Twemoji scalable emoji example"));
  const PPRect *geometry=&pp_app_ui(life.app)->geometry;
  CHECK(geometry->x==120 && geometry->y==140 && geometry->w==960 && geometry->h==720);
  int entries=0;
  const SDL_TrayEntry **menu=SDL_GetTrayEntries(SDL_GetTrayMenu(life.tray->native),&entries);
  CHECK(entries==2 && !strcmp(SDL_GetTrayEntryLabel((SDL_TrayEntry *)menu[0]),"Settings"));
  SDL_ClickTrayEntry((SDL_TrayEntry *)menu[0]);
  PPCommandKind queued; CHECK(pp_tray_take(life.tray,&queued) && queued==PP_COMMAND_SHOW_SETTINGS);
  CHECK(pp_lifecycle_action(&life,queued,&error));
  life.settings->render_hidden=true;
  CHECK(!strcmp(SDL_GetWindowTitle(life.settings->backend->window),"Yoink Settings"));
  pp_settings_draw(life.settings);
  CHECK(life.settings->backend->width==960 && life.settings->backend->height==720);
  CHECK(!life.settings->failed && !(SDL_GetWindowFlags(life.settings->backend->window)&SDL_WINDOW_INPUT_FOCUS));
  CHECK(pp_app_command(life.app,&(PPCommand){.kind=PP_COMMAND_RENAME_PROJECT,.name="Renamed \xc3\xa9 project"},&error));
  CHECK(pp_app_command(life.app,&(PPCommand){.kind=PP_COMMAND_SAVE_AS,.path=saved},&error));
  pp_lifecycle_stop(&life);
  CHECK(pp_lifecycle_start(&life,NULL,&error));
  CHECK(!strcmp(pp_app_name(life.app),"Renamed \xc3\xa9 project") && !strcmp(pp_app_path(life.app),saved));
  CHECK(pp_app_ui(life.app)->geometry.w==960 && pp_app_ui(life.app)->geometry.h==720);
  pp_lifecycle_stop(&life);
  legacy_preferences_directory=NULL;
  CHECK(SDL_RemovePath(saved)); CHECK(SDL_RemovePath(preferences)); CHECK(SDL_RemovePath(directory));
  puts("PASS Yoink Settings via tray; legacy preference identity, stored geometry and existing .pointy fixture restore; Unicode project-name save/reopen");
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
  if(argc==2 && !strcmp(argv[1],"--font-disclosure-recovery")) { test_failed_font_disclosure(); return 0; }
  if(argc==2 && !strcmp(argv[1],"--public-identity")) { test_public_identity(); return 0; }
  if(argc==2 && !strcmp(argv[1],"--benchmark")) { benchmark("empty");benchmark("static");benchmark("gif");return 0; }
  test_gl_failure(); test_failed_settings(); test_persistent_settings_failure(); test_failed_font_disclosure();
  test_failed_settings_save(); test_failed_settings_save_dialog();
  test_lifecycle(); test_last_project(); return 0;
}
