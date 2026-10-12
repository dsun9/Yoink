/* The production-source probe's visible runtime protocol. Included after its
 * measurement, source-fixture and bounded injection helpers. */
#include "receiver_shared.h"
extern uint64_t pp_probe_density_display;
extern double pp_probe_drawable_density;
typedef struct RuntimeObservation {
  double complete_ms, handled_ms, ready_observed_ms,first_presentation_owner_ms;
  PPPlatformEvent wheel;
  double endpoint;
  unsigned draws, completed, gpu, wheels;
  PPProbeMetric phases[PROBE_COUNT];
} RuntimeObservation;
static bool runtime_operation(PPRuntime *runtime,uint64_t id,RuntimeObservation *out) {
  double gap=pp_probe_last_owner_end_ms?pp_probe_clock()-pp_probe_last_owner_end_ms:0;
  const char *prior_phase=pp_probe_owner_phase;pp_probe_owner_phase="platform-poll-runtime-event-ticks";
  memset(out,0,sizeof(*out));memset(pp_probe_metrics,0,sizeof(pp_probe_metrics));
  double start=pp_probe_clock();
  PPSprite *sprite=pp_sprites_find(runtime->presenter,id);
  if(sprite&&sprite->content&&pp_content_requested(sprite->content)&&
      !pp_runtime_presentation_metrics(runtime).worker_pending_jobs)
    out->ready_observed_ms=start;
  PPPlatformEvent event;
  while(pp_platform_poll(&event)) {
    if(event.kind==PP_EVENT_QUIT){pp_probe_puts("INCOMPLETE platform quit during runtime operation");return false;}
    bool wheel=event.kind==PP_EVENT_WHEEL&&event.instance_id==id;
    pp_runtime_event(runtime,&event);
    if(runtime->failed){pp_probe_printf("INCOMPLETE event runtime failure: %s\n",runtime->error.message);return false;}
    if(wheel) {
      out->wheel=event;out->wheels++;out->handled_ms=pp_probe_clock();
      sprite=pp_sprites_find(runtime->presenter,id);
      if(!sprite)return false;
      out->endpoint=sprite->ease_target.longest_dip;
    }
    /* The shipping lifecycle ticks after each delivered native event. */
    pp_runtime_tick(runtime,pp_probe_clock()/1000.0);
    pp_probe_active_worker=runtime->worker;
    if(runtime->failed){pp_probe_printf("INCOMPLETE tick runtime failure: %s\n",runtime->error.message);return false;}
    if(pp_probe_clock()-start>250||pp_probe_injected_max_wait>100)break;
  }
  pp_runtime_tick(runtime,pp_probe_clock()/1000.0);
  pp_probe_active_worker=runtime->worker;
  out->complete_ms=pp_probe_clock()-start;
  pp_probe_last_owner_end_ms=pp_probe_clock();
  if(gap>50)pp_probe_printf("GAP between_owner_operations_ms=%.3f prior_phase=%s\n",gap,prior_phase);
  memcpy(out->phases,pp_probe_metrics,sizeof(out->phases));
  out->draws=out->phases[PROBE_GPU_SUBMIT].count;
  out->completed=out->phases[PROBE_GPU_COMPLETE].count;
  out->gpu=out->phases[PROBE_GPU_EXECUTION].count;
  if(out->complete_ms>50) {
    pp_probe_printf("SLOW runtime_operation_ms=%.3f draws=%u completed=%u gpu=%u\n",out->complete_ms,out->draws,out->completed,out->gpu);
    for(unsigned i=0;i<PROBE_COUNT;i++)if(out->phases[i].count)
      pp_probe_printf("SLOW phase=%s count=%u sum_ms=%.3f max_ms=%.3f\n",stage_names[i],out->phases[i].count,out->phases[i].ms,out->phases[i].max_ms);
  }
  return !runtime->failed&&pp_scene_get(pp_app_scene(runtime->app),id)!=NULL;
}
static void runtime_wait(PPRuntime *runtime) {
  pp_probe_owner_phase="native-scheduler-wait";
  double now=pp_probe_clock()/1000.0;
  PPSchedule schedule=pp_runtime_schedule(runtime,now);
  double delay=pp_next_wake(&schedule,now)-pp_probe_clock()/1000.0;
  if(delay>0)MsgWaitForMultipleObjectsEx(0,NULL,(DWORD)ceil(delay*1000),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
}
static void observation_add(RuntimeObservation *total,const RuntimeObservation *part) {
  if(!total->draws&&part->draws)total->first_presentation_owner_ms=part->complete_ms;
  total->complete_ms+=part->complete_ms;total->draws+=part->draws;
  total->completed+=part->completed;total->gpu+=part->gpu;total->wheels+=part->wheels;
  if(part->ready_observed_ms&&!total->ready_observed_ms)total->ready_observed_ms=part->ready_observed_ms;
  if(part->wheels){total->wheel=part->wheel;total->handled_ms=part->handled_ms;total->endpoint=part->endpoint;}
  for(unsigned stage=0;stage<PROBE_COUNT;stage++) {
    total->phases[stage].count+=part->phases[stage].count;
    total->phases[stage].ms+=part->phases[stage].ms;
    total->phases[stage].max_ms=fmax(total->phases[stage].max_ms,part->phases[stage].max_ms);
  }
}
static bool runtime_safe(const RuntimeObservation *o,Acceptance *a,double deadline) {
  if(pp_probe_clock()>deadline)a->timed_out=true;
  if(o->complete_ms>250||pp_probe_injected_max_wait>100)a->stopped=true;
  if(a->stopped||a->timed_out)pp_probe_printf("STOP runtime_owner_ms=%.3f tagged_max_wait_ms=%u timed_out=%d\n",o->complete_ms,pp_probe_injected_max_wait,a->timed_out);
  return !a->stopped&&!a->timed_out;
}
static bool runtime_settle(PPRuntime *runtime,uint64_t id,Acceptance *a,double deadline,RuntimeObservation *work) {
  double limit=fmin(deadline,pp_probe_clock()+2000);
  do {
    RuntimeObservation o;
    if(!runtime_operation(runtime,id,&o)){pp_probe_puts("INCOMPLETE runtime settle operation or removed measured sprite");return false;}
    if(work) {
      observation_add(work,&o);cold_observation(a,o.draws,o.completed,o.gpu);
      if(o.draws!=o.completed||o.draws!=o.gpu) {
        pp_probe_printf("INCOMPLETE cold operation draws=%u completed=%u gpu=%u\n",o.draws,o.completed,o.gpu);
        return false;
      }
    }
    if(!runtime_safe(&o,a,deadline))return false;
    PPSprite *s=pp_sprites_find(runtime->presenter,id);
    if(s&&!s->resizing&&!s->rotating&&!s->raster_requested&&!s->raster_retry&&s->retained.valid)return true;
    runtime_wait(runtime);
  }while(pp_probe_clock()<limit);
  pp_probe_puts("INCOMPLETE settling runtime source/pose");return false;
}
static bool runtime_wheel(PPRuntime *runtime,uint64_t id,int delta,RuntimeObservation *observed,NativeWheel *wheel,Acceptance *a,double deadline) {
  unsigned hooks_before=pp_probe_wheel_count;
  memset(observed,0,sizeof(*observed));
  *wheel=(NativeWheel){.delta=delta};
  HANDLE thread=CreateThread(NULL,0,send_wheel,wheel,0,NULL);
  if(!thread)return false;
  bool found=false;double limit=fmin(deadline,pp_probe_clock()+1000);
  do {
    RuntimeObservation o;
    if(!runtime_operation(runtime,id,&o)||!runtime_safe(&o,a,deadline))break;
    observation_add(observed,&o);
    if(o.wheels){found=observed->wheels==1;break;}
    runtime_wait(runtime);
  }while(pp_probe_clock()<limit);
  /* Service hook callbacks while the owned helper returns from SendInput. */
  for(unsigned i=0;i<100&&WaitForSingleObject(thread,0)==WAIT_TIMEOUT;i++) {
    RuntimeObservation tail;
    if(!runtime_operation(runtime,id,&tail)||!runtime_safe(&tail,a,deadline)){found=false;break;}
    observation_add(observed,&tail);
    Sleep(1);
  }
  bool exited=WaitForSingleObject(thread,0)==WAIT_OBJECT_0;
  if(exited)CloseHandle(thread);
  else {pp_probe_finish_output(false);ExitProcess(2);}
  if(!found) {
    PPVec2 pointer={0};pp_platform_pointer(&pointer);
    PPSprite *s=pp_sprites_find(runtime->presenter,id);
    pp_probe_printf("INCOMPLETE native_wheel accepted=%u tagged_wheel_hooks=%u normalized_measured_wheels=%u actual_pointer=%.0f,%.0f sprite_id=%llu bounds=%.0f,%.0f,%.0f,%.0f elapsed_helper_ms=%.3f\n",wheel->accepted,pp_probe_wheel_count-hooks_before,observed->wheels,pointer.x,pointer.y,(unsigned long long)id,s?s->retained.bounds.x:0,s?s->retained.bounds.y:0,s?s->retained.bounds.w:0,s?s->retained.bounds.h:0,wheel->returned-wheel->sent);
  }
  return found&&wheel->accepted==1&&runtime_safe(observed,a,deadline);
}
typedef struct ProbeReceiver {HANDLE mapping,job;ReceiverState *state;PROCESS_INFORMATION child;} ProbeReceiver;
/* A suspended child enters a private kill-on-close job before it can create
 * any window. Closing this probe, including ExitProcess, kills its receiver. */
static bool owned_child(ProbeReceiver *r,const char *exe,char *command) {
  r->job=CreateJobObjectW(NULL,NULL);if(!r->job)return false;
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};
  limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if(!SetInformationJobObject(r->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return false;
  STARTUPINFOA startup={.cb=sizeof(startup)};
  if(!CreateProcessA(exe,command,NULL,NULL,FALSE,CREATE_SUSPENDED,NULL,NULL,&startup,&r->child))return false;
  if(!AssignProcessToJobObject(r->job,r->child.hProcess)||ResumeThread(r->child.hThread)==(DWORD)-1) {
    TerminateProcess(r->child.hProcess,2);WaitForSingleObject(r->child.hProcess,1000);return false;
  }
  return true;
}
static void receiver_close(ProbeReceiver *r) {
  if(r->state&&r->state->window)PostMessageW((HWND)r->state->window,WM_CLOSE,0,0);
  if(r->child.hProcess) {
    if(WaitForSingleObject(r->child.hProcess,2000)==WAIT_TIMEOUT){TerminateProcess(r->child.hProcess,2);WaitForSingleObject(r->child.hProcess,1000);}
    CloseHandle(r->child.hThread);CloseHandle(r->child.hProcess);
  }
  if(r->state)UnmapViewOfFile(r->state);if(r->mapping)CloseHandle(r->mapping);
  if(r->job)CloseHandle(r->job);
  memset(r,0,sizeof(*r));
}
static bool receiver_open(ProbeReceiver *r,PPVec2 center) {
  char name[80],exe[MAX_PATH],command[MAX_PATH+100];
  snprintf(name,sizeof(name),"PointyPointer.Performance.%lu",GetCurrentProcessId());
  r->mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(ReceiverState),name);
  if(!r->mapping)return false;
  r->state=MapViewOfFile(r->mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ReceiverState));if(!r->state)return false;
  memset(r->state,0,sizeof(*r->state));
  if(!GetModuleFileNameA(NULL,exe,sizeof(exe)))return false;
  char *slash=strrchr(exe,'\\');if(!slash)return false;strcpy(slash+1,"pointy_input_receiver.exe");
  snprintf(command,sizeof(command),"\"%s\" %s",exe,name);
  if(!owned_child(r,exe,command))return false;
  double deadline=pp_probe_clock()+2000;
  while(!r->state->window&&pp_probe_clock()<deadline){pump();Sleep(10);}
  if(!r->state->window)return false;
  HWND window=(HWND)r->state->window;
  return SetWindowPos(window,HWND_NOTOPMOST,(int)center.x-180,(int)center.y-160,360,320,SWP_NOACTIVATE|SWP_SHOWWINDOW)!=0;
}
static int receiver_ownership_control(void) {
  /* CPU/process-only self child returns before application/SDL initialization. */
  ProbeReceiver r={0};char exe[MAX_PATH],command[MAX_PATH+80];
  if(!GetModuleFileNameA(NULL,exe,sizeof(exe)))return 1;
  snprintf(command,sizeof(command),"\"%s\" --ownership-child",exe);
  if(!owned_child(&r,exe,command)){receiver_close(&r);return 1;}
  CloseHandle(r.job);r.job=NULL;
  bool killed=WaitForSingleObject(r.child.hProcess,1000)==WAIT_OBJECT_0;
  receiver_close(&r);
  pp_probe_puts(killed?"PASS owned child terminates on private job close before any desktop/SDL calls":"FAIL private owned child survived job close");
  return killed?0:1;
}
static bool runtime_sharp(PPRuntime *runtime,uint64_t id,Acceptance *a,double deadline) {
  double latencies[ACCEPTANCE_SAMPLES]={0},preparation[ACCEPTANCE_SAMPLES]={0},owner_max[ACCEPTANCE_SAMPLES]={0};
  bool ok=send_alt(true);if(!ok)return false;
  pump();
  for(unsigned sample=0;ok&&sample<ACCEPTANCE_SAMPLES;sample++) {
    if(!runtime_settle(runtime,id,a,deadline,NULL)){ok=false;break;}
    PPSprite *sprite=pp_sprites_find(runtime->presenter,id);
    PPPresentationMetrics before=pp_runtime_presentation_metrics(runtime);
    unsigned pending_before=pp_probe_worker_input_count,hook_before=pp_probe_injected_hook_count;
    RuntimeObservation o={0};NativeWheel wheel;
    if(!runtime_wheel(runtime,id,2,&o,&wheel,a,deadline)){ok=false;break;}
    double expected_timestamp=o.wheel.timestamp,expected_endpoint=o.endpoint;
    uint64_t expected_revision=pp_item_snapshot_revision(pp_scene_get(pp_app_scene(runtime->app),id)->snapshot);
    double handled=o.handled_ms,ready=o.ready_observed_ms,operation_max=o.complete_ms;
    unsigned draws=o.draws,completed=o.completed,gpu=o.gpu;
    double limit=fmin(deadline,expected_timestamp*1000+500);
    PPPresentationMetrics after=pp_runtime_presentation_metrics(runtime);
    while(after.sharp_publications==before.sharp_publications&&pp_probe_clock()<limit) {
      runtime_wait(runtime);
      if(!runtime_operation(runtime,id,&o)||!runtime_safe(&o,a,deadline)){ok=false;break;}
      if(o.ready_observed_ms&&!ready)ready=o.ready_observed_ms;
      operation_max=fmax(operation_max,o.complete_ms);draws+=o.draws;completed+=o.completed;gpu+=o.gpu;
      after=pp_runtime_presentation_metrics(runtime);
    }
    double publication=pp_probe_clock(),elapsed=publication-expected_timestamp*1000;
    sprite=pp_sprites_find(runtime->presenter,id);
    bool match=sprite&&sprite->retained.valid&&after.sharp_publications==before.sharp_publications+1&&
      after.last_sharp_instance_id==id&&after.last_sharp_content_revision==expected_revision&&
      after.last_sharp_input_timestamp==expected_timestamp&&after.last_sharp_longest_dip==expected_endpoint&&
      after.last_sharp_drawable_density==1.5&&draws>0&&draws==completed&&draws==gpu;
    bool pending=before.worker_pending_jobs==0&&pp_probe_worker_input_count>pending_before&&
      after.worker_requests==before.worker_requests+1&&after.worker_completions==before.worker_completions+1&&
      after.worker_source_builds==before.worker_source_builds+1;
    pp_probe_printf("SHARP sample=%u match=%d pending_overlap=%d event_timestamp=%.9f published_input_timestamp=%.9f endpoint=%.9f published_endpoint=%.9f content_revision=%llu published_revision=%llu density=%.2f input_to_completed_publication_ms=%.3f helper_to_hook_ms=%.3f hook_to_handled_ms=%.3f ready_observed_qpc_ms=%.3f ready_to_publication_upper_ms=%.3f worker_source_ms=%.3f operation_max_ms=%.3f draws=%u completed=%u gpu=%u pending_hooks=%u all_hooks=%u\n",
      sample,match,pending,expected_timestamp,after.last_sharp_input_timestamp,expected_endpoint,after.last_sharp_longest_dip,
      (unsigned long long)expected_revision,(unsigned long long)after.last_sharp_content_revision,after.last_sharp_drawable_density,
      elapsed,pp_probe_last_wheel_hook_ms-wheel.sent,handled-expected_timestamp*1000,ready,ready?publication-ready:0,
      after.worker_source_ms-before.worker_source_ms,operation_max,draws,completed,gpu,pp_probe_worker_input_count-pending_before,pp_probe_injected_hook_count-hook_before);
    if(!ok||!match||!pending){ok=false;break;}
    a->sharp_observations++;a->matched_sharp_observations++;a->pending_sharp_observations++;
    latencies[sample]=elapsed;preparation[sample]=after.worker_source_ms-before.worker_source_ms;owner_max[sample]=operation_max;
    a->max_sharp_ms=fmax(a->max_sharp_ms,elapsed);
    if(elapsed>200||pp_probe_injected_max_wait>50){ok=false;break;}
  }
  if(!send_alt(false))ok=false;
  if(a->sharp_observations==ACCEPTANCE_SAMPLES) {
    statistics("latest_input_to_completed_sharp",latencies);
    statistics("sharp_private_generation",preparation);statistics("sharp_owner_operation_max",owner_max);
  }
  return ok&&a->sharp_observations==ACCEPTANCE_SAMPLES;
}
static int native_acceptance(PPApp *app,int diagnostic_kind) {
  Acceptance a={.release=!strcmp(PP_PROBE_CONFIGURATION,"Release")};
  PPRuntime *runtime=NULL;PPError error={0};PPItemSnapshot *snapshot=NULL;ProbeReceiver receiver={0};
  HANDLE injector=NULL;PPDisplay *displays=NULL;size_t count=0;PPVec2 pointer;uint64_t id=0;
  /* Establish this private probe process's default before saving a thread
   * scope, as the native receiver fixtures do. No global display setting is
   * changed. SDL later observes the already established process default. */
  SetLastError(0);bool process_dpi=SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)!=0;
  DWORD process_dpi_error=process_dpi?0:GetLastError();
  DPI_AWARENESS_CONTEXT previous_dpi=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  bool scoped_dpi=previous_dpi!=NULL;
  pp_probe_printf("PROBE DPI process_default_set=%d process_error=%lu saved_scope=%p saved_scope_valid=%d saved_awareness=%d\n",process_dpi,process_dpi_error,(void *)previous_dpi,IsValidDpiAwarenessContext(previous_dpi),GetAwarenessFromDpiAwarenessContext(previous_dpi));
  double deadline=pp_probe_clock()+145000,begin=pp_probe_clock();
  pp_probe_completion_enabled=true;
  if(!scoped_dpi)goto done;
  if(!pp_platform_displays(&displays,&count,&error)||!pp_platform_pointer(&pointer)||!count)goto done;
  const PPDisplay *selected=pp_display_at(displays,count,pointer);if(!selected)selected=displays;
  PPDisplay display=*selected;pp_displays_free(displays);displays=NULL;
  PPVec2 center={display.bounds.x+display.bounds.w*.5,display.bounds.y+display.bounds.h*.5};
  pp_probe_density_display=display.id;
  if(!receiver_open(&receiver,center)||!SetCursorPos((int)center.x,(int)center.y))goto done;
  PPVec2 positioned={0};if(!pp_platform_pointer(&positioned))goto done;
  pp_probe_printf("POSITION requested_physical_center=%.0f,%.0f actual_physical_cursor_before_SDL=%.0f,%.0f\n",center.x,center.y,positioned.x,positioned.y);
  if(!pp_runtime_create(&runtime,app,&error))goto done;
  pp_runtime_tick(runtime,pp_probe_clock()/1000.0);if(runtime->failed)goto done;
  pp_probe_printf("PROTOCOL build=%s backend=direct-retained-gl matrix=visible-native-hook-platform-poll-runtime-interaction-easing-shared-worker warmup=%u samples=%u required_cases=96 complete_boundary=owner-operation-through-all-draws-returned-swap-GPU-completion-publication compositor_visible=unobserved cold_runtime_before_tagged_helper_ms=%.3f actual_desktop_density=%.3f density_simulation=both-logical-ratios-physical-monitor-bounds canonical_font_size_dip=96 input_clock=GetTickCount-ms-plus-QPC p95=nearest-rank\n",PP_PROBE_CONFIGURATION,ACCEPTANCE_WARMUP,ACCEPTANCE_SAMPLES,pp_probe_clock()-begin,display.drawable_pixels_per_dip);
  pp_probe_printf("GPU renderer=\"%s\" vendor=\"%s\" version=\"%s\"\n",glGetString(GL_RENDERER),glGetString(GL_VENDOR),glGetString(GL_VERSION));
  inject_stop=CreateEventW(NULL,TRUE,FALSE,NULL);if(!inject_stop)goto done;
  injection_limit=18000;injector=CreateThread(NULL,0,zero_moves,NULL,0,NULL);if(!injector)goto done;
  const double sizes[]={96,384,768,1536},densities[]={1,1.5},angles[]={0,17.25};
  const char *names[]={"default-emoji","bitmap-noto","png-alpha-fixture","gif-disposal-fixture","svg-arrow-fixture","mixed-text"};
  for(int kind=diagnostic_kind<0?0:diagnostic_kind;kind<(diagnostic_kind<0?6:diagnostic_kind+1);kind++) {
    PPFontInfo font;double source_start=pp_probe_clock();
    if(!acceptance_source(app,kind,&snapshot,&font,&error))goto done;
    pp_probe_printf("SOURCE kind=%s canonical_font_size_dip=96 snapshot_cold_ms=%.3f revision=%llu font=\"%s\" quality=see-supervised-scalable-quality-and-bitmap-retained\n",names[kind],pp_probe_clock()-source_start,(unsigned long long)pp_item_snapshot_revision(snapshot),font.family);
    if(kind==0)a.default_segoe=!strcmp(font.family,"Segoe UI Emoji");
    for(int density=0;density<2;density++)for(int size=0;size<4;size++)for(int angle=0;angle<2;angle++) {
      pp_probe_drawable_density=densities[density];
      PPPose pose={center,sizes[size],angles[angle],1};
      double cold=pp_probe_clock();
      PPPresentationMetrics cold_before=pp_runtime_presentation_metrics(runtime);
      PPSourceMetrics cold_sources_before=pp_assets_source_metrics(pp_app_assets(app));
      RuntimeObservation cold_work={0};
      pp_probe_owner_phase="case-setup-report";
      pp_probe_printf("SETUP source=%s dip=%.0f density=%.2f angle=%.2f\n",names[kind],sizes[size],densities[density],angles[angle]);pp_probe_fflush(stdout);
      double scene_start=pp_probe_clock();pp_probe_owner_phase="scene-restore-or-create";
      if(!id) {
        PPSprite *s=pp_sprites_create_snapshot(runtime->presenter,snapshot,PP_ANCHOR_TOP_LEFT,pose,cold/1000,&error);
        if(!s)goto done;id=s->id;
        if(!pp_scene_set_pose(pp_app_scene(app),id,pose,&error))goto done;
      }else if(!pp_scene_restore(pp_app_scene(app),id,snapshot,pose,cold/1000,&error))goto done;
      double scene_ms=pp_probe_clock()-scene_start;
      if(scene_ms>50)pp_probe_printf("GAP scene_create_restore_ms=%.3f\n",scene_ms);
      pp_probe_active_worker=runtime->worker;
      if(!runtime_settle(runtime,id,&a,deadline,&cold_work))goto done;
      cold_case_complete(&a,cold_work.draws);
      if(!cold_work.draws){pp_probe_puts("INCOMPLETE cold case has no completed draw");goto done;}
      pp_probe_active_worker=runtime->worker;
      PPPresentationMetrics cold_after=pp_runtime_presentation_metrics(runtime);
      pp_probe_printf("COLD source=%s dip=%.0f density=%.2f angle=%.2f settled_wall_ms=%.3f owner_work_ms=%.3f first_presentation_owner_ms=%.3f private_generation_ms=%.3f private_builds=%llu owner_builds=%llu contexts=%u context_creation_ms=%.3f source_upload_ms=%.3f cpu_draw_submission_ms=%.3f gpu_execution_ms=%.3f gpu_completion_wait_ms=%.3f returned_native_swap_ms=%.3f native_size_placement_ms=%.3f draws=%u completed=%u gpu=%u tagged_input_max_ms=%u\n",
        names[kind],sizes[size],densities[density],angles[angle],pp_probe_clock()-cold,cold_work.complete_ms,cold_work.first_presentation_owner_ms,
        cold_after.worker_source_ms-cold_before.worker_source_ms,(unsigned long long)(cold_after.worker_source_builds-cold_before.worker_source_builds),
        (unsigned long long)(pp_assets_source_metrics(pp_app_assets(app)).raster_builds-cold_sources_before.raster_builds),
        cold_work.phases[PROBE_RENDERER_CREATE].count,cold_work.phases[PROBE_RENDERER_CREATE].ms,cold_work.phases[PROBE_UPLOAD].ms,
        cold_work.phases[PROBE_GPU_SUBMIT].ms,cold_work.phases[PROBE_GPU_EXECUTION].ms,cold_work.phases[PROBE_GPU_COMPLETE].ms,
        cold_work.phases[PROBE_GL_SWAP].ms,cold_work.phases[PROBE_NATIVE_SIZE].ms+cold_work.phases[PROBE_NATIVE_PLACE].ms,
        cold_work.draws,cold_work.completed,cold_work.gpu,pp_probe_injected_max_wait);
      PPSprite *realized=pp_sprites_find(runtime->presenter,id);
      SDL_Window *window=SDL_GetWindowFromID(pp_surface_window_id(realized->surface));
      int pixel_w=0,pixel_h=0;RECT native={0};
      HWND hwnd=SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,NULL);
      if(!SDL_GetWindowSizeInPixels(window,&pixel_w,&pixel_h)||!GetWindowRect(hwnd,&native))goto done;
      pp_platform_pointer(&positioned);
      pp_probe_printf("REALIZED drawable_px=%dx%d native_px=%ldx%ld native_origin=%ld,%ld desktop_units_per_dip=%.3f drawable_pixels_per_dip=%.3f actual_pose=%.9f,%.9f actual_pointer=%.0f,%.0f\n",pixel_w,pixel_h,native.right-native.left,native.bottom-native.top,native.left,native.top,densities[density],densities[density],realized->pose.longest_dip,realized->pose.angle_deg,positioned.x,positioned.y);
      if(pixel_w!=native.right-native.left||pixel_h!=native.bottom-native.top||realized->pose.longest_dip!=sizes[size]||realized->pose.angle_deg!=angles[angle]||positioned.x<native.left||positioned.x>=native.right||positioned.y<native.top||positioned.y>=native.bottom){pp_probe_puts("INCOMPLETE requested pose/drawable/native/physical target mapping");goto done;}
      pp_probe_printf("CASE source=%s dip=%.0f density=%.2f actual_density=%.2f simulated=%d rotation=%.2f cold_setup_and_settle_ms=%.3f input_max_ms=%u\n",names[kind],sizes[size],densities[density],display.drawable_pixels_per_dip,densities[density]!=display.drawable_pixels_per_dip,angles[angle],pp_probe_clock()-cold,pp_probe_injected_max_wait);
      double values[7][ACCEPTANCE_SAMPLES]={{0}};unsigned warm=0,observed=0,total_draws=0;
      unsigned input_before=pp_probe_injected_hook_count;
      PPRendererMetrics rb=pp_runtime_renderer_metrics(runtime);PPPresentationMetrics pb=pp_runtime_presentation_metrics(runtime);
      uint64_t builds=pp_assets_source_metrics(pp_app_assets(app)).raster_builds;
      for(unsigned sample=0;sample<ACCEPTANCE_WARMUP+ACCEPTANCE_SAMPLES;sample++) {
        RuntimeObservation o={0};NativeWheel wheel;
        if(!runtime_wheel(runtime,id,(sample&1)?-1:1,&o,&wheel,&a,deadline))goto done;
        bool active=o.wheels==1&&o.draws>0&&o.draws==o.completed&&o.draws==o.gpu;
        PPSprite *sprite=pp_sprites_find(runtime->presenter,id);
        if(!active||!sprite||!sprite->rotating){pp_probe_puts("INCOMPLETE active transform or all-draw completion");goto done;}
        total_draws+=o.draws;
        pp_probe_printf("%s sample=%u owner_ms=%.3f helper_ms=%.3f input_timestamp=%.9f pose_size=%.9f pose_angle=%.9f draws=%u completed=%u gpu=%u\n",sample<3?"WARMUP":"ACTIVE",sample<3?sample:sample-3,o.complete_ms,wheel.returned-wheel.sent,o.wheel.timestamp,sprite->pose.longest_dip,sprite->pose.angle_deg,o.draws,o.completed,o.gpu);
        if(sample<ACCEPTANCE_WARMUP){warm++;a.warmup_observations++;}
        else {
          values[0][observed]=o.complete_ms;values[1][observed]=o.phases[PROBE_SOURCE_REQUEST].ms;
          values[2][observed]=o.phases[PROBE_GPU_SUBMIT].ms;values[3][observed]=o.phases[PROBE_GPU_EXECUTION].ms;
          values[4][observed]=o.phases[PROBE_GPU_COMPLETE].ms;values[5][observed]=o.phases[PROBE_GL_SWAP].ms;
          values[6][observed]=o.phases[PROBE_NATIVE_SIZE].ms+o.phases[PROBE_NATIVE_PLACE].ms;
          observed++;a.samples++;a.active_samples++;a.gpu_observations++;
        }
      }
      pp_probe_owner_phase="case-statistics-and-report";double report_start=pp_probe_clock();
      double p95=statistics("complete_owner",values[0]);a.regression_p95_ms=fmax(a.regression_p95_ms,p95);
      const char *metrics[]={"source_request","cpu_draw_submission","gpu_execution","cpu_wait_gpu_completion","returned_native_swap","native_size_and_placement"};
      for(unsigned i=1;i<7;i++)statistics(metrics[i-1],values[i]);
      a.completed_cases++;if(pp_probe_injected_hook_count>input_before)a.native_input_cases++;
      if(kind==0&&size==3&&density==1){a.required_timing_cases++;a.required_p95_ms=fmax(a.required_p95_ms,p95);}
      PPRendererMetrics ra=pp_runtime_renderer_metrics(runtime);PPPresentationMetrics pa=pp_runtime_presentation_metrics(runtime);
      uint64_t source_builds=pp_assets_source_metrics(pp_app_assets(app)).raster_builds-builds+pa.worker_source_builds-pb.worker_source_builds;
      uint64_t uploads=ra.source_uploads-rb.source_uploads,draws=ra.transform_draws-rb.transform_draws;
      pp_probe_printf("OBSERVATIONS warmup=%u active=%u all_completed_draws=%u source_builds=%llu uploads=%llu transform_draws=%llu readbacks=%llu native_publications=%llu all_tagged_hooks=%u input_max_ms=%u hook_callback_max_ms=%.3f\n",warm,observed,total_draws,
        (unsigned long long)source_builds,(unsigned long long)uploads,(unsigned long long)draws,
        (unsigned long long)(ra.readbacks-rb.readbacks),(unsigned long long)(pa.native_publications-pb.native_publications),pp_probe_injected_hook_count-input_before,pp_probe_injected_max_wait,pp_probe_hook_max_ms);
      pp_probe_fflush(stdout);
      double report_ms=pp_probe_clock()-report_start;
      if(report_ms>50)pp_probe_printf("GAP statistics_and_flush_ms=%.3f\n",report_ms);
      if(draws!=total_draws||(kind!=3&&(source_builds||uploads))){pp_probe_puts("INCOMPLETE all-draw accounting or static-source rotation reuse");a.stopped=true;goto done;}
    }
    /* Run sharp at the required largest density before replacing default art. */
    if(kind==0&&!runtime_sharp(runtime,id,&a,deadline))goto done;
    pp_item_snapshot_release(snapshot);snapshot=NULL;
  }
done:
  if(error.code)pp_probe_printf("INCOMPLETE error=\"%s\"\n",error.message);
  if(runtime&&runtime->failed)pp_probe_printf("INCOMPLETE runtime_error=\"%s\"\n",runtime->error.message);
  if(injector&&!stop_injector(injector)){pp_probe_finish_output(false);ExitProcess(2);}
  else if(!injector&&inject_stop){CloseHandle(inject_stop);inject_stop=NULL;}
  pp_probe_active_worker=NULL;a.max_input_wait_ms=pp_probe_injected_max_wait;
  pp_probe_printf("INPUT FINAL all_tagged_hooks=%u wheels=%u pending_hooks=%u max_wait_ms=%u max_callback_ms=%.3f last_kind=%u\n",pp_probe_injected_hook_count,pp_probe_wheel_count,pp_probe_worker_input_count,pp_probe_injected_max_wait,pp_probe_hook_max_ms,pp_probe_last_input_kind);
  pp_item_snapshot_release(snapshot);pp_runtime_destroy(runtime);receiver_close(&receiver);pp_displays_free(displays);
  pp_probe_drawable_density=0;pp_probe_density_display=0;pp_probe_completion_enabled=false;
  if(scoped_dpi) {
    SetLastError(0);DPI_AWARENESS_CONTEXT returned=SetThreadDpiAwarenessContext(previous_dpi);
    DWORD restore_error=returned?0:GetLastError();DPI_AWARENESS_CONTEXT current=GetThreadDpiAwarenessContext();
    bool equal=AreDpiAwarenessContextsEqual(current,previous_dpi),restored=returned!=NULL&&equal;
    pp_probe_printf("PROBE DPI saved_scope=%p saved_scope_valid_after=%d restore_return=%p restore_error=%lu current_scope=%p current_awareness=%d equivalent=%d restored=%d\n",(void *)previous_dpi,IsValidDpiAwarenessContext(previous_dpi),(void *)returned,restore_error,(void *)current,GetAwarenessFromDpiAwarenessContext(current),equal,restored);
    if(!restored)a.stopped=true;
  }
  if(diagnostic_kind<0)return acceptance_result(&a);
  bool ok=a.release&&!a.stopped&&!a.timed_out&&a.completed_cases==16&&a.cold_cases==16&&
    !a.cold_mismatches&&a.cold_draws>=a.cold_cases&&a.cold_draws==a.cold_completed&&a.cold_draws==a.cold_gpu&&
    a.samples==320&&a.warmup_observations==48&&a.active_samples==320&&a.gpu_observations==320&&a.native_input_cases==16&&a.max_input_wait_ms<=50;
  if(diagnostic_kind==0)ok=ok&&a.default_segoe&&a.required_timing_cases==2&&a.required_p95_ms<=16.7&&a.sharp_observations==20&&a.max_sharp_ms<=200;
  pp_probe_printf("RESULT visible_source_diagnostic=%s kind=%d cases=%u/16 warmup=%u/48 active=%u/320 required_p95_ms=%.3f sharp=%u/20 sharp_max_ms=%.3f input_max_ms=%.3f overall_96_case_acceptance=not-evaluated compositor_visible=unobserved\n",ok?"PASS":"FAIL",diagnostic_kind,a.completed_cases,a.warmup_observations,a.active_samples,a.required_p95_ms,a.sharp_observations,a.max_sharp_ms,a.max_input_wait_ms);
  return ok?0:1;
}
