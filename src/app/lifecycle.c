#include "lifecycle.h"
#include "runtime_internal.h"
#include "tray.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static double seconds(void) { return (double)SDL_GetPerformanceCounter()/SDL_GetPerformanceFrequency(); }
bool pp_lifecycle_start(PPLifecycle *life, const PPAppOptions *options, PPError *error) {
  *life=(PPLifecycle){0};
  if (!pp_app_create(&life->app,options,error) ||
      !pp_runtime_create(&life->runtime,life->app,error) ||
      !pp_tray_create(&life->tray,life->app,error)) {
    pp_lifecycle_stop(life); return false;
  }
  pp_runtime_prepare_ui(life->runtime);
  pp_runtime_tick(life->runtime,seconds());
  if(life->runtime->failed) {
    if(error) *error=life->runtime->error;
    pp_lifecycle_stop(life); return false;
  }
  return true;
}
void pp_lifecycle_stop(PPLifecycle *life) {
  if(!life) return;
  pp_tray_destroy(life->tray);
  pp_platform_release_capture();
  pp_runtime_destroy(life->runtime);
  pp_settings_destroy(life->settings); /* Detaches copied dialog results. */
  pp_app_destroy(life->app);
  *life=(PPLifecycle){0};
}
static bool settings(PPLifecycle *life, PPError *error) {
  if(!life->settings && !pp_settings_create(&life->settings,life->app,error)) return false;
  return pp_app_command(life->app,&(PPCommand){.kind=PP_COMMAND_SHOW_SETTINGS},error);
}
bool pp_lifecycle_action(PPLifecycle *life, PPCommandKind command, PPError *error) {
  if(!pp_settings_recover(life->settings,error))
    return pp_app_ui(life->app)->quit_requested;
  if(command==PP_COMMAND_SHOW_SETTINGS) return settings(life,error);
  if(!pp_settings_can_transition(life->settings)) {
    settings(life,error);
    return false;
  }
  bool ok=pp_app_command(life->app,&(PPCommand){.kind=command},error);
  if(pp_app_ui(life->app)->transition_pending) ok=settings(life,error)&&ok;
  return ok;
}
bool pp_lifecycle_iteration(PPLifecycle *life, PPError *error) {
  PPPlatformEvent event; bool quit=false;
  while(pp_ui_poll(&event)) {
    if(event.kind==PP_EVENT_QUIT) quit=true;
    else { pp_settings_event(life->settings,&event); pp_runtime_event(life->runtime,&event); }
  }
  /* Validate edits before queued tray transitions or native sprite pulls. */
  pp_settings_draw(life->settings);
  if(!life->settings) pp_platform_poll_dialogs();
  PPCommandKind action;
  if(quit) pp_lifecycle_action(life,PP_COMMAND_QUIT,error);
  if(pp_tray_take(life->tray,&action)) pp_lifecycle_action(life,action,error);
  while(pp_platform_poll(&event)) {
    if(event.kind==PP_EVENT_QUIT) pp_lifecycle_action(life,PP_COMMAND_QUIT,error);
    else pp_runtime_event(life->runtime,&event);
    pp_runtime_tick(life->runtime,seconds());
  }
  double now=seconds();
  pp_runtime_tick(life->runtime,now);
  pp_runtime_draw_ui(life->runtime,now);
  if(life->runtime->failed) { if(error) *error=life->runtime->error; return false; }
  PPSchedule schedule=pp_runtime_schedule(life->runtime,now);
  if(pp_app_ui(life->app)->settings_open) schedule.ui=now+1.0/60;
  if(life->tray->pending || pp_app_ui(life->app)->quit_requested) schedule.command=now;
  double delay=pp_next_wake(&schedule,now)-seconds();
  if(delay>0) pp_tray_wait((unsigned)ceil(delay*1000));
  return true;
}
int pp_app_run(int argc, char **argv) {
  bool smoke=argc==2 && !strcmp(argv[1],"--smoke-test");
  bool open_settings=argc==2 && !strcmp(argv[1],"--settings");
  if(argc>2 || (argc==2 && !smoke && !open_settings)) {
    fputs("Usage: PointyPointer [--settings | --smoke-test]\n",stderr); return 2;
  }
  PPLifecycle life={0}; PPError error={0}; int result=1;
  PPAppOptions isolated={.preferences_directory=""};
  if(!pp_lifecycle_start(&life,smoke?&isolated:NULL,&error)) goto done;
  if(open_settings && !settings(&life,&error)) goto done;
  double end=seconds()+1.0;
  while(!pp_app_ui(life.app)->quit_requested) {
    if(!pp_lifecycle_iteration(&life,&error)) goto done;
    double now=seconds();
    if(smoke && now>=end) {
      printf("PASS smoke: tray/runtime, default Noto resource, eight anchors, no settings; %s\n",SDL_GetBasePath());
      break;
    }
  }
  result=0;
done:
  if(result) fprintf(stderr,"PointyPointer: %s\n",error.message);
  pp_lifecycle_stop(&life);
  SDL_Quit();
  return result;
}
