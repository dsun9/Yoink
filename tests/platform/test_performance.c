/* Bounded, opt-in stage measurements of production sources. No shipped telemetry.
 * Run under the desktop supervisor: hidden contexts can still affect the desktop. */
#include "performance_probe.h"
#include "../../src/ui/backend.h"
#include "../../src/ui/drawer_internal.h"
#include "../../src/app/presenter_internal.h"
#include "../../src/app/runtime_internal.h"
#include "../test.h"
#include <miniz.h>
#include <math.h>
#include <string.h>
__declspec(thread) PPProbeMetric pp_probe_metrics[PROBE_COUNT];
bool pp_probe_completion_enabled;
static struct {const char *family; bool found; size_t bytes; double ms;} font_events[32];
static unsigned font_count;
void pp_probe_font(const char *family, bool found, size_t bytes, double ms) {
  if(font_count<32) {font_events[font_count].family=family;font_events[font_count].found=found;font_events[font_count].bytes=bytes;font_events[font_count++].ms=ms;}
}
double pp_probe_clock(void) {
  LARGE_INTEGER now, frequency; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
  return 1000.0 * now.QuadPart / frequency.QuadPart;
}
void pp_probe_record(enum PPProbeStage stage, double start) {
  double elapsed = pp_probe_clock()-start;
  PPProbeMetric *m = &pp_probe_metrics[stage]; m->count++; m->ms += elapsed;
  if (elapsed > m->max_ms) m->max_ms = elapsed;
}
typedef struct Acceptance {
  unsigned completed_cases, samples, gpu_observations, native_input_cases, sharp_observations, required_timing_cases;
  double required_p95_ms, regression_p95_ms, max_input_wait_ms, max_sharp_ms;
  bool release, default_segoe, stopped, timed_out;
} Acceptance;
enum { ACCEPTANCE_CASES=96, ACCEPTANCE_WARMUP=3, ACCEPTANCE_SAMPLES=20 };
static int acceptance_result(const Acceptance *a) {
  bool failed=false;
#define GATE(condition, message) do { if(!(condition)) { puts("FAIL " message); failed=true; } } while(0)
  GATE(a->release,"acceptance requires Release configuration");
  GATE(a->default_segoe,"required default Segoe UI Emoji source is unavailable or not selected");
  GATE(!a->stopped,"protective early stop leaves required cases incomplete");
  GATE(!a->timed_out,"acceptance timeout");
  GATE(a->completed_cases==ACCEPTANCE_CASES,"incomplete required source/size/density/rotation cases");
  GATE(a->samples==ACCEPTANCE_CASES*ACCEPTANCE_SAMPLES,"missing repeated warm samples");
  GATE(a->gpu_observations==a->samples,"missing GPU execution/completion observations");
  GATE(a->native_input_cases==ACCEPTANCE_CASES,"missing native injected input observations in required cases");
  GATE(a->sharp_observations>=ACCEPTANCE_SAMPLES,"missing native resize-input-to-sharp-publication observations");
  GATE(a->required_timing_cases==2,"missing required default Segoe complete-path timing cases");
  if(a->required_timing_cases) GATE(isfinite(a->required_p95_ms) && a->required_p95_ms<=16.7,"required default Segoe complete warm path p95 exceeds 16.7 ms");
  GATE(isfinite(a->max_input_wait_ms) && a->max_input_wait_ms<=50,"maximum native input-arrival wait exceeds 50 ms");
  GATE(isfinite(a->max_sharp_ms) && a->max_sharp_ms<=200,"latest resize-input-to-sharp publication exceeds 200 ms");
#undef GATE
  char required[32]="unavailable",sharp[32]="unavailable";
  if(a->required_timing_cases) snprintf(required,sizeof(required),"%.3f",a->required_p95_ms);
  if(a->sharp_observations) snprintf(sharp,sizeof(sharp),"%.3f",a->max_sharp_ms);
  printf("RESULT acceptance=%s cases=%u/%u samples=%u/%u required_timing_cases=%u/2 required_p95_ms=%s regression_worst_p95_ms=%.3f input_max_ms=%.3f native_sharp_samples=%u sharp_max_ms=%s\n",
    failed?"FAIL":"PASS",a->completed_cases,ACCEPTANCE_CASES,a->samples,ACCEPTANCE_CASES*ACCEPTANCE_SAMPLES,a->required_timing_cases,required,a->regression_p95_ms,a->max_input_wait_ms,a->sharp_observations,sharp);
  fflush(stdout); return failed?1:0;
}
static int acceptance_control(const char *control) {
  puts("CONTROL synthetic protocol observations; no native acceptance was executed");
  Acceptance a={.completed_cases=96,.samples=1920,.gpu_observations=1920,.native_input_cases=96,
    .sharp_observations=20,.required_timing_cases=2,.required_p95_ms=10,.max_input_wait_ms=5,.max_sharp_ms=20,.release=true,.default_segoe=true};
  if(!strcmp(control,"budget")) a.required_p95_ms=16.8;
  else if(!strcmp(control,"input")) a.max_input_wait_ms=51;
  else if(!strcmp(control,"sharp")) a.max_sharp_ms=200.1;
  else if(!strcmp(control,"stop")) a.stopped=true;
  else if(!strcmp(control,"missing")) a.native_input_cases=a.gpu_observations=a.sharp_observations=0;
  else if(!strcmp(control,"incomplete")) a.completed_cases=95;
  else if(!strcmp(control,"timeout")) { Sleep(10000); a.timed_out=true; }
  else if(strcmp(control,"success")) { fprintf(stderr,"Unknown acceptance control: %s\n",control); return 2; }
  return acceptance_result(&a);
}
static void report(const char *scenario, double elapsed) {
  static const char *names[] = {"text", "raster", "inspect", "find_font", "renderer_create", "transform", "ui_create", "upload", "ui_end_swap", "surface", "dib", "layered", "readback", "texture", "source_request", "cpu_draw_submission", "gpu_execution", "cpu_wait_gpu_completion"};
  printf("SCENARIO %s wall_ms=%.3f\n", scenario, elapsed);
  for (int i=0;i<PROBE_COUNT;i++) if (pp_probe_metrics[i].count)
    printf("STAGE %s count=%u sum_ms=%.3f max_ms=%.3f\n", names[i], pp_probe_metrics[i].count, pp_probe_metrics[i].ms, pp_probe_metrics[i].max_ms);
  for(unsigned i=0;i<font_count;i++) printf("FONT family=%s found=%d bytes=%zu ms=%.3f\n",font_events[i].family,font_events[i].found,font_events[i].bytes,font_events[i].ms);
  font_count=0;
  memset(pp_probe_metrics, 0, sizeof(pp_probe_metrics)); fflush(stdout);
}
static bool ladder(PPApp *app, double density, bool require_budget) {
  const PPProject *project = pp_app_project(app);
  PPItemSnapshot *snapshot = pp_project_snapshot(project, pp_project_target(project, PP_ANCHOR_TOP_LEFT)->id);
  PPVec2 size = pp_item_snapshot_size(snapshot);
  PPRenderer *renderer = NULL; PPError error = {0}; CHECK(pp_renderer_create(&renderer, &error));
  printf("GPU %s density=%.3f canonical=%.0fx%.0f\n", glGetString(GL_RENDERER), density, size.x, size.y);
  memset(pp_probe_metrics,0,sizeof(pp_probe_metrics));
  const double sizes[] = {96,384,768,1536};
  bool complete=true;
  for (int i=0;i<4;i++) {
    double start=pp_probe_clock(), next; const PPFrame *source=NULL; PPFrame final={0};
    CHECK(pp_item_snapshot_frame(snapshot,0,sizes[i]*density/fmax(size.x,size.y),&source,&next,&error));
    CHECK(pp_renderer_transform(renderer,source,sizes[i],0,1,density,&final,&error));
    double elapsed=pp_probe_clock()-start;
    double raster_ms=pp_probe_metrics[PROBE_RASTER].ms;
    printf("ZOOM dip=%.0f source=%dx%d output=%dx%d bytes=%zu\n",sizes[i],source->width,source->height,final.width,final.height,pp_assets_raster_bytes(pp_app_assets(app)));
    pp_frame_destroy(&final); report("zoom",elapsed);
    if(require_budget && sizes[i]==1536) CHECK(raster_ms<100);
    if (elapsed>250) { puts("STOP zoom escalation: frame exceeded 250ms"); complete=false;break; }
  }
  pp_item_snapshot_release(snapshot); pp_renderer_destroy(renderer);
  return complete;
}
static void drawer(PPApp *app, bool require_cache, bool large, bool require_warm) {
  const char *path=PP_MEDIA_FIXTURES "/alpha.png";
  if(large) {
    path=PP_PROBE_OUTPUT "/performance-1024.png";
    SDL_Surface *image=SDL_CreateSurface(1024,768,SDL_PIXELFORMAT_RGBA32); CHECK(image);
    CHECK(SDL_FillSurfaceRect(image,NULL,SDL_MapSurfaceRGBA(image,20,100,220,255)));
    size_t png_bytes=0; void *png_data=tdefl_write_image_to_png_file_in_memory(image->pixels,image->w,image->h,4,&png_bytes); CHECK(png_data);
    FILE *file=fopen(path,"wb"); CHECK(file && fwrite(png_data,1,png_bytes,file)==png_bytes); fclose(file); mz_free(png_data); SDL_DestroySurface(image);
  }
  PPError error={0}; size_t bytes; void *png=SDL_LoadFile(path,&bytes); CHECK(png);
  PPItem item={.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=1};
  CHECK(pp_asset_import(pp_app_assets(app),(PPBytes){png,bytes},PP_ASSET_PNG,&item.image_asset,&error)); SDL_free(png);
  CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_ADD_ITEM,.anchor=PP_ANCHOR_TOP_LEFT,.item=&item},&error));
  PPAsset *asset=pp_asset_retain(pp_app_assets(app),item.image_asset); PPVec2 native=pp_asset_intrinsic_size(asset);
  printf("PNG native=%.0fx%.0f list_count=%zu\n",native.x,native.y,pp_project_anchor(pp_app_project(app),PP_ANCHOR_TOP_LEFT)->count); pp_asset_release(asset);
  PPRuntime *runtime=NULL;
  PPDisplay display={1,{-20000,-20000,1920,1080},1,1};
  if(require_warm) {
    double warm_start=pp_probe_clock();
    CHECK(pp_runtime_create(&runtime,app,&error)); pp_runtime_prepare_ui(runtime);
    CHECK(runtime->drawer_cache.backend && !runtime->drawer_cache.backend->visible);
    PPDisplay *displays=NULL;size_t count=0;PPVec2 pointer;
    CHECK(pp_platform_displays(&displays,&count,&error));CHECK(pp_platform_pointer(&pointer));
    const PPDisplay *chosen=pp_display_at(displays,count,pointer);CHECK(chosen);display=*chosen;pp_displays_free(displays);
    display.bounds.x-=20000;display.bounds.y-=20000;
    report("runtime_warmup",pp_probe_clock()-warm_start);
  }
  uint64_t id=0; CHECK(pp_scene_spawn(pp_app_scene(app),pp_app_project(app),PP_ANCHOR_TOP_LEFT,(PPVec2){display.bounds.x+200,display.bounds.y+200},0,&id,&error));
  for (int open=0;open<3;open++) {
    PPDrawer *d=NULL; double start=pp_probe_clock();
    CHECK(pp_drawer_open(&d,app,id,&error)); d->render_hidden=true;
    if(require_warm) {
      CHECK(pp_drawer_use_cache(d,&runtime->drawer_cache,&error));
      CHECK(!d->backend->focus_id && !d->backend->input_started && !d->backend->composing && !d->backend->modal && !d->backend->popup_id);
      CHECK(d->backend->context.input.keyboard.text_len==0);
    }
    pp_drawer_layout(d,&display,1);
    CHECK(pp_drawer_draw(d,0,&error)); double elapsed=pp_probe_clock()-start;
    unsigned creates=pp_probe_metrics[PROBE_UI_CREATE].count;
    printf("DRAWER open=%d density=%.3f\n",open,d->backend->scale); report("drawer_first",elapsed);
    if(require_warm) CHECK(creates==0);
    for(int frame=0;frame<3;frame++) { start=pp_probe_clock(); CHECK(pp_drawer_draw(d,0,&error));
      double frame_ms=pp_probe_clock()-start; unsigned uploads=pp_probe_metrics[PROBE_UPLOAD].count; report("drawer_unchanged",frame_ms);
      if(require_cache) CHECK(uploads==0);
    }
    if(require_cache) {
      const PPAnchorList *list=pp_project_anchor(pp_app_project(app),PP_ANCHOR_TOP_LEFT);
      uint64_t text_id=list->item_ids[0], image_id=list->item_ids[1];
      PPItemSnapshot *snapshot=pp_project_snapshot(pp_app_project(app),text_id);
      const PPFrame *source; double next;
      CHECK(pp_item_snapshot_frame(snapshot,0,2,&source,&next,&error)); pp_item_snapshot_release(snapshot);
      memset(pp_probe_metrics,0,sizeof(pp_probe_metrics));
      CHECK(pp_drawer_draw(d,0,&error)); CHECK(pp_probe_metrics[PROBE_UPLOAD].count==0);
      PPItem changed=*pp_project_item(pp_app_project(app),text_id); changed.text="A\xf0\x9f\x91\x89";
      CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_EDIT_ITEM,.item_id=text_id,.item=&changed},&error));
      CHECK(pp_drawer_draw(d,0,&error)); CHECK(pp_probe_metrics[PROBE_UPLOAD].count==1);
      memset(pp_probe_metrics,0,sizeof(pp_probe_metrics));
      CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_REORDER_ITEM,.anchor=PP_ANCHOR_TOP_LEFT,.item_id=image_id,.index=0},&error));
      CHECK(pp_drawer_draw(d,0,&error)); CHECK(pp_probe_metrics[PROBE_UPLOAD].count==2);
      pp_drawer_close(d); puts("PASS drawer cache: unchanged/cross-consumer hit, edited identity and reordered slots invalidate"); break;
    }
    if(require_warm) {
      d->backend->focus_id=123;d->backend->composing=true;d->backend->modal=true;d->backend->popup_id=99;
      pp_drawer_event(d,&(PPPlatformEvent){.kind=PP_EVENT_TEXT,.window_id=pp_ui_backend_window_id(d->backend),.codepoint='X'});
      SDL_Event stale={.type=SDL_EVENT_KEY_DOWN};stale.key.windowID=pp_ui_backend_window_id(d->backend);stale.key.key=SDLK_ESCAPE;CHECK(SDL_PushEvent(&stale));
    }
    pp_drawer_close(d);
    if(require_warm) {
      CHECK(!runtime->drawer_cache.in_use && runtime->drawer_cache.backend && !runtime->drawer_cache.backend->visible);
      CHECK(pp_scene_spawn(pp_app_scene(app),pp_app_project(app),PP_ANCHOR_TOP_LEFT,(PPVec2){display.bounds.x+200,display.bounds.y+200},0,&id,&error));
    }
  }
  if(require_warm) {
    PPDrawer *d=NULL;CHECK(pp_drawer_open(&d,app,id,&error));d->render_hidden=true;CHECK(pp_drawer_use_cache(d,&runtime->drawer_cache,&error));
    display.desktop_units_per_dip*=1.25;display.drawable_pixels_per_dip*=1.25;
    pp_drawer_layout(d,&display,1);CHECK(!d->backend);
    CHECK(pp_drawer_draw(d,0,&error));CHECK(pp_probe_metrics[PROBE_UI_CREATE].count==1);
    pp_drawer_close(d);pp_runtime_destroy(runtime);
    puts("PASS runtime warm cache first/repeated opens, changed instances, reset input/focus/modal state, density rebuild and teardown");
  }
}
static void raster_reference(PPApp *app, bool save) {
  PPTextStyle style=pp_project_target(pp_app_project(app),PP_ANCHOR_TOP_LEFT)->text_style;
  const char *cases[]={"\xf0\x9f\x91\x89","A\xf0\x9f\x91\x89","\xf0\x9f\x91\x89\xf0\x9f\x8f\xbd","\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb","\xf0\x9f\x91\x89\n\xf0\x9f\x91\x89"};
  const double scales[]={.45,1,2.333,4.1}; PPError error={0};
  for(int i=0;i<5;i++) for(int j=0;j<4;j++) {
    PPFrame frame={0}; style.color[3]=(i&1)?137:255;
    CHECK(pp_text_render(pp_app_assets(app),cases[i],&style,scales[j],&frame,NULL,&error));
    char path[512]; snprintf(path,sizeof(path),PP_PROBE_OUTPUT "/raster-reference-%d-%d.bin",i,j);
    FILE *file=fopen(path,save?"wb":"rb"); CHECK(file); size_t count=(size_t)frame.width*frame.height;
    if(save) {CHECK(fwrite(&frame.width,sizeof(int),1,file)==1);CHECK(fwrite(&frame.height,sizeof(int),1,file)==1);CHECK(fwrite(frame.pixels,4,count,file)==count);CHECK(fwrite(frame.content_alpha,1,count,file)==count);}
    else {int w,h;CHECK(fread(&w,sizeof(int),1,file)==1 && fread(&h,sizeof(int),1,file)==1);CHECK(w==frame.width && h==frame.height);uint8_t *pixels=malloc(count*5);CHECK(pixels && fread(pixels,5,count,file)==count);CHECK(!memcmp(pixels,frame.pixels,count*4));CHECK(!memcmp(pixels+count*4,frame.content_alpha,count));free(pixels);}
    fclose(file); pp_frame_destroy(&frame);
  }
  printf("PASS %s 20 original RGBA/coverage rasters, mixed outlines, clusters, opacity and fractional scales\n",save?"saved":"byte-identical");
}
static void drawer_first_layout_density(PPApp *app) {
  PPError error={0};PPRenderer *renderer=NULL;
  CHECK(pp_renderer_create(&renderer,&error));
  PPDrawerCache cache={.renderer=renderer};
  uint64_t id=0;
  CHECK(pp_scene_spawn(pp_app_scene(app),pp_app_project(app),PP_ANCHOR_TOP_LEFT,
      (PPVec2){-19000,-19000},0,&id,&error));
  const double densities[]={1,2,1};
  PPDisplay display={1,{-20000,-20000,4000,4000},1,1};
  CHECK(pp_drawer_cache_warm(&cache,&display,&error));
  for(int i=1;i<3;i++) {
    PPDrawer *d=NULL;
    CHECK(pp_drawer_open(&d,app,id,&error));
    CHECK(pp_drawer_use_cache(d,&cache,&error));
    CHECK(!d->render_hidden && !d->backend->visible);
    CHECK(!(SDL_GetWindowFlags(d->backend->window)&SDL_WINDOW_INPUT_FOCUS));
    display.desktop_units_per_dip=densities[i];
    display.drawable_pixels_per_dip=densities[i];
    pp_drawer_layout(d,&display,1);
    printf("FIRST LAYOUT density %.0f -> %.0f dismissed=%d\n",densities[i-1],densities[i],d->dismissed);
    CHECK(!d->dismissed && !d->backend && pp_drawer_valid(d));
    /* Only suppress native show AFTER exercising ordinary first-layout state. */
    d->render_hidden=true;
    CHECK(pp_drawer_draw(d,0,&error));
    CHECK(d->backend && !d->dismissed && !d->backend->visible);
    pp_drawer_close(d);
    CHECK(!cache.in_use && cache.backend);
  }
  /* Keep the existing visible-but-unfocused dismissal rule. The UI visibility
   * flag is the production history record; no foreground change is needed to
   * exercise its focus-lost density branch with this hidden window. */
  PPDrawer *active=NULL;
  CHECK(pp_drawer_open(&active,app,id,&error));
  CHECK(pp_drawer_use_cache(active,&cache,&error));
  active->backend->visible=true;
  display.desktop_units_per_dip=display.drawable_pixels_per_dip=2;
  pp_drawer_layout(active,&display,1);
  CHECK(active->dismissed && !active->backend);
  pp_drawer_close(active);
  pp_drawer_cache_destroy(&cache);pp_renderer_destroy(renderer);
  puts("PASS production first-layout density increase/decrease rebuild without dismissal; previously visible unfocused drawer still dismisses");
}
static void font_source(void) {
  PPBlob font={0}; PPError error={0}; PPFontInfo info;
  CHECK(pp_platform_find_font("Segoe UI Semibold",&font,&error));
  CHECK(pp_font_inspect((PPBytes){font.data,font.size},0,&info,&error));
  CHECK(!strcmp(info.family,"Segoe UI") && !strcmp(info.style,"Semibold"));
  pp_blob_destroy(&font); puts("PASS UI styled font resolves actual family/style metadata");
}
static double edge_width(const PPFrame *frame) {
  double total=0;unsigned count=0;
  for(int y=0;y<frame->height;y++) for(int x=1;x<frame->width;x++) {
    const uint8_t *row=frame->content_alpha+(size_t)y*frame->width;
    if(row[x]<230) continue;
    int low=x-1;while(low>0 && row[low]>25) low--;
    if(row[low]<=25) {total+=x-low;count++;} break;
  }
  return count?total/count:0;
}
static void save_frame(const PPFrame *frame,const char *name) {
  char path[512];snprintf(path,sizeof(path),PP_PROBE_OUTPUT "/%s.bmp",name);
  SDL_Surface *surface=SDL_CreateSurfaceFrom(frame->width,frame->height,SDL_PIXELFORMAT_RGBA32,frame->pixels,(int)frame->stride);
  CHECK(surface && SDL_SaveBMP(surface,path));SDL_DestroySurface(surface);
}
static void blur_profile(PPApp *app) {
  PPError error={0};const PPItem *item=pp_project_target(pp_app_project(app),PP_ANCHOR_TOP_LEFT);
  PPAsset *font=pp_asset_retain(pp_app_assets(app),item->text_style.fallback_asset);PPFontInfo info;
  CHECK(pp_font_inspect(pp_asset_source(font),item->text_style.fallback_face,&info,&error));pp_asset_release(font);
  PPFrame strike={0};CHECK(pp_text_render(pp_app_assets(app),item->text,&item->text_style,info.bitmap_em_size/item->text_style.font_size_dip,&strike,NULL,&error));
  printf("BITMAP source=%s fixed_em=%.1f layout=%dx%d edge_10_90_mean_px=%.4f\n",info.family,info.bitmap_em_size,strike.width,strike.height,edge_width(&strike));save_frame(&strike,"blur-strike");
  PPItemSnapshot *snapshot=pp_project_snapshot(pp_app_project(app),item->id);PPVec2 size=pp_item_snapshot_size(snapshot);
  PPRenderer *renderer=NULL;CHECK(pp_renderer_create(&renderer,&error));
  double sizes[]={96,384,1536};
  for(int i=0;i<3;i++) {
    const PPFrame *source;double next;PPFrame final={0};
    CHECK(pp_item_snapshot_frame(snapshot,0,sizes[i]/fmax(size.x,size.y),&source,&next,&error));
    CHECK(pp_renderer_transform(renderer,source,sizes[i],0,1,1,&final,&error));
    double before=edge_width(source)*(double)final.width/source->width,after=edge_width(&final);
    printf("BLUR dip=%.0f before=%dx%d final=%dx%d before_edge_in_final_pixels=%.4f after_edge_pixels=%.4f extra=%.4f\n",sizes[i],source->width,source->height,final.width,final.height,before,after,after-before);
    char name[64];snprintf(name,sizeof(name),"blur-before-%.0f",sizes[i]);save_frame(source,name);snprintf(name,sizeof(name),"blur-after-%.0f",sizes[i]);save_frame(&final,name);pp_frame_destroy(&final);
  }
  pp_frame_destroy(&strike);pp_item_snapshot_release(snapshot);pp_renderer_destroy(renderer);
}
extern unsigned pp_probe_hook_count, pp_probe_hook_max_wait;
extern double pp_probe_hook_max_ms;
static HANDLE inject_stop;
static volatile LONG injected_moves;
static int injection_limit=250;
static DWORD WINAPI zero_moves(void *unused) {
  (void)unused;
  for(int i=0;i<injection_limit && WaitForSingleObject(inject_stop,10)==WAIT_TIMEOUT;i++) {
    INPUT input={0}; input.type=INPUT_MOUSE; input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_MOVE_NOCOALESCE;
    input.mi.dx=(i&1)?-1:1; /* Bounded alternating motion; supervisor restores cursor. */
    input.mi.dwExtraInfo=PP_PROBE_INPUT_TAG; input.mi.time=GetTickCount();
    if(SendInput(1,&input,sizeof(input))==1) InterlockedIncrement(&injected_moves);
  }
  return 0;
}
static void pump(void) {PPPlatformEvent event;while(pp_platform_poll(&event)) {}}
static bool native_profile(PPApp *app) {
  PPError error={0}; PPRenderer *renderer=NULL; PPSurface *surface=NULL;
  CHECK(pp_renderer_create(&renderer,&error)); CHECK(pp_surface_create(&surface,&error));
  PPItemSnapshot *snapshot=pp_project_snapshot(pp_app_project(app),pp_project_target(pp_app_project(app),PP_ANCHOR_TOP_LEFT)->id);
  PPAnchorPresenter presenter={.renderer=renderer,.assets=pp_app_assets(app)};
  PPDisplay display={1,{-20000,-20000,10000,10000},1.5,1.5};
  struct NativeSample {double dip,ms;PPProbeMetric metrics[PROBE_COUNT];} samples[12]; unsigned count=0;
  size_t retained=0; double next;
  inject_stop=CreateEventW(NULL,TRUE,FALSE,NULL); CHECK(inject_stop);
  HANDLE injector=CreateThread(NULL,0,zero_moves,NULL,0,NULL);CHECK(injector);
  const double sizes[]={96,384,768,1536}; bool stopped=false;
  for(int size=0;size<4 && !stopped;size++) for(int frame=0;frame<3;frame++) {
    pump(); memset(pp_probe_metrics,0,sizeof(pp_probe_metrics)); double start=pp_probe_clock();
    CHECK(pp_present_content(&presenter,surface,snapshot,(PPPose){{-19000+frame*2,-19000},sizes[size]+frame*.1,0,1},&display,0,&next,&retained,&error));
    samples[count].ms=pp_probe_clock()-start;samples[count].dip=sizes[size]+frame*.1;
    memcpy(samples[count].metrics,pp_probe_metrics,sizeof(pp_probe_metrics));count++;
    pump();
    if(samples[count-1].ms>250 || pp_probe_hook_max_wait>100) {stopped=true;break;}
  }
  SetEvent(inject_stop);
  for(int i=0;i<200 && WaitForSingleObject(injector,0)==WAIT_TIMEOUT;i++){pump();Sleep(10);}
  CHECK(WaitForSingleObject(injector,0)==WAIT_OBJECT_0);CloseHandle(injector);CloseHandle(inject_stop);
  pp_surface_destroy(surface);pp_assets_release_raster(pp_app_assets(app),retained);
  pp_item_snapshot_release(snapshot);pp_renderer_destroy(renderer);
  printf("NATIVE density=1.5 injected=%ld hook_count=%u max_arrival_wait_ms=%u max_callback_ms=%.3f stopped=%d\n",injected_moves,pp_probe_hook_count,pp_probe_hook_max_wait,pp_probe_hook_max_ms,stopped);
  for(unsigned i=0;i<count;i++){memcpy(pp_probe_metrics,samples[i].metrics,sizeof(pp_probe_metrics));printf("NATIVE dip=%.1f\n",samples[i].dip);report("native",samples[i].ms);}
  CHECK(injected_moves>0 && pp_probe_hook_count>0);
  return !stopped;
}
static int number_order(const void *a,const void *b) {
  double x=*(const double *)a,y=*(const double *)b; return (x>y)-(x<y);
}
static double statistics(const char *name,double values[ACCEPTANCE_SAMPLES]) {
  qsort(values,ACCEPTANCE_SAMPLES,sizeof(*values),number_order);
  double median=(values[9]+values[10])*.5,p95=values[18];
  printf("DISTRIBUTION metric=%s samples=%u median_ms=%.3f p95_ms=%.3f max_ms=%.3f\n",name,ACCEPTANCE_SAMPLES,median,p95,values[19]);
  return p95;
}
static void selected_font(PPAssets *assets,PPAssetId id,int face_index,const char *role,PPFontInfo *info) {
  PPError error={0}; PPAsset *font=pp_asset_retain(assets,id);
  memset(info,0,sizeof(*info));
  if(!font || !pp_font_inspect(pp_asset_source(font),face_index,info,&error)) {
    printf("FONT role=%s unavailable error=%s\n",role,error.message); pp_asset_release(font); return;
  }
  printf("FONT role=%s family=\"%s\" style=\"%s\" version=\"%s\" color=%d scalable=%d bitmap_strikes=%d color_layers=%d colr_version=%u bitmap_em=%.1f\n",
    role,info->family,info->style,info->version[0]?info->version:"unavailable",info->color,info->scalable,info->bitmap_strikes,info->color_layers,info->colr_version,info->bitmap_em_size);
  pp_asset_release(font);
}
static bool acceptance_source(PPApp *app,int kind,PPItemSnapshot **out,PPFontInfo *font,PPError *error) {
  PPItem item=*pp_project_target(pp_app_project(app),PP_ANCHOR_TOP_LEFT);
  PPAssets *assets=pp_app_assets(app); memset(font,0,sizeof(*font));
  if(kind==1) {
    size_t size=0; void *bytes=SDL_LoadFile(PP_PROBE_ROOT "/resources/fonts/NotoColorEmoji.ttf",&size);
    if(!bytes) return false;
    bool ok=pp_asset_import(assets,(PPBytes){bytes,size},PP_ASSET_FONT,&item.text_style.fallback_asset,error);
    SDL_free(bytes); if(!ok) return false;
    item.text_style.fallback_face=0;
  } else if(kind==2 || kind==3) {
    size_t size=0; void *bytes=SDL_LoadFile(kind==2 ? PP_MEDIA_FIXTURES "/alpha.png" : PP_MEDIA_FIXTURES "/disposal.gif",&size);
    if(!bytes) return false;
    bool ok=pp_asset_import(assets,(PPBytes){bytes,size},kind==2?PP_ASSET_PNG:PP_ASSET_GIF,&item.image_asset,error);
    SDL_free(bytes); if(!ok) return false; item.kind=PP_ITEM_IMAGE; item.image_longest_dip=96;
  } else if(kind==4) {
    const char *svg="<svg width='96' height='64'><rect x='4' y='4' width='88' height='56' rx='12' fill='#e66c32'/><path d='M16 32L68 32M52 16L68 32L52 48' fill='none' stroke='white' stroke-width='6'/></svg>";
    if(!pp_asset_import(assets,(PPBytes){(const uint8_t *)svg,strlen(svg)},PP_ASSET_SVG,&item.image_asset,error)) return false;
    item.kind=PP_ITEM_IMAGE; item.image_longest_dip=96;
  } else if(kind==5) item.text="A\xf0\x9f\x91\x89\nB";
  if(item.kind==PP_ITEM_TEXT) {
    if(kind==5) {
      PPFontInfo primary;
      selected_font(assets,item.text_style.primary_asset,item.text_style.primary_face,"primary",&primary);
    }
    selected_font(assets,item.text_style.fallback_asset,item.text_style.fallback_face,"emoji-fallback",font);
  }
  return pp_item_snapshot_create(assets,&item,out,error);
}
static bool stop_injector(HANDLE thread) {
  SetEvent(inject_stop);
  for(int i=0;i<100 && WaitForSingleObject(thread,0)==WAIT_TIMEOUT;i++) {pump(); Sleep(10);}
  bool stopped=WaitForSingleObject(thread,0)==WAIT_OBJECT_0;
  if(stopped) {CloseHandle(thread); CloseHandle(inject_stop); inject_stop=NULL;}
  return stopped;
}
static bool send_alt(bool pressed) {
  INPUT input={.type=INPUT_KEYBOARD}; input.ki.wVk=VK_MENU; input.ki.dwFlags=pressed?0:KEYEVENTF_KEYUP;
  return SendInput(1,&input,sizeof(input))==1;
}
static bool sharp_native_samples(PPApp *app,PPRenderer *renderer,Acceptance *acceptance,double deadline) {
  PPDisplay *displays=NULL; size_t count=0; PPError error={0}; PPVec2 pointer;
  if(!pp_platform_displays(&displays,&count,&error) || !pp_platform_pointer(&pointer) || !count) {pp_displays_free(displays); return false;}
  const PPDisplay *selected=pp_display_at(displays,count,pointer); if(!selected) selected=displays;
  PPDisplay display=*selected; display.drawable_pixels_per_dip=1.5; pp_displays_free(displays);
  PPAnchorPresenter p={.renderer=renderer,.assets=pp_app_assets(app),.project=(PPProject *)pp_app_project(app),.scene=pp_app_scene(app),.displays=&display,.display_count=1};
  pp_platform_gesture_config(&p.gestures);
  double now=pp_probe_clock()/1000.0;
  PPSprite *sprite=pp_sprites_create(&p,PP_ANCHOR_TOP_LEFT,(PPPose){{display.bounds.x+display.bounds.w*.5,display.bounds.y+display.bounds.h*.5},1536,0,1},now,&error);
  if(!sprite) return false;
  uint64_t id=sprite->id; bool ok=false,alt=false;
  if(!pp_sprites_update(&p,now,&error)) goto done;
  const PPSceneInstance *instance=pp_scene_get(p.scene,id); const PPFrame *source; double next;
  PPVec2 logical=pp_item_snapshot_size(instance->snapshot);
  if(!pp_item_snapshot_frame(instance->snapshot,0,1536*1.5/fmax(logical.x,logical.y),&source,&next,&error)) goto done;
  double factor=1536*display.desktop_units_per_dip/fmax(source->width,source->height),best=INFINITY; POINT target={0};
  for(int y=0;y<source->height;y+=4) for(int x=0;x<source->width;x+=4) {
    if(source->content_alpha[(size_t)y*source->width+x]<250) continue;
    double dx=(x+.5-source->width*.5)*factor,dy=(y+.5-source->height*.5)*factor;
    double px=sprite->pose.center.x+dx,py=sprite->pose.center.y+dy;
    if(px<display.bounds.x+24 || py<display.bounds.y+24 || px>=display.bounds.x+display.bounds.w-24 || py>=display.bounds.y+display.bounds.h-24) continue;
    if(dx*dx+dy*dy<best) {best=dx*dx+dy*dy;target=(POINT){(LONG)round(px),(LONG)round(py)};}
  }
  if(!isfinite(best) || !SetCursorPos(target.x,target.y) || !send_alt(true)) goto done;
  alt=true; pump(); Sleep(20);
  for(unsigned sample=0;sample<ACCEPTANCE_SAMPLES;sample++) {
    if(pp_probe_clock()>deadline) {acceptance->timed_out=true; break;}
    unsigned wheel_before=pp_probe_wheel_count; uint64_t sharp_before=p.metrics.sharp_publications;
    INPUT input={.type=INPUT_MOUSE}; input.mi.dwFlags=MOUSEEVENTF_WHEEL;
    input.mi.mouseData=(DWORD)((sample&1)?-1:1); input.mi.time=GetTickCount(); input.mi.dwExtraInfo=PP_PROBE_INPUT_TAG;
    double sent=pp_probe_clock(); if(SendInput(1,&input,sizeof(input))!=1) break;
    double limit=sent+500;
    while(pp_probe_clock()<limit && p.metrics.sharp_publications==sharp_before) {
      PPPlatformEvent event;
      while(pp_platform_poll(&event)) if(!pp_anchor_presenter_event(&p,&event,&error)) goto done;
      double frame_start=pp_probe_clock();
      if(!pp_sprites_update(&p,frame_start/1000.0,&error)) goto done;
      if(pp_probe_clock()-frame_start>250 || pp_probe_injected_max_wait>100) {acceptance->stopped=true;goto done;}
      if(p.metrics.sharp_publications==sharp_before) Sleep(1);
    }
    double ms=pp_probe_clock()-sent;
    if(p.metrics.sharp_publications!=sharp_before+1 || pp_probe_wheel_count!=wheel_before+1 ||
       p.metrics.last_sharp_instance_id!=id || p.metrics.last_sharp_content_revision!=pp_item_snapshot_revision(instance->snapshot) ||
       p.metrics.last_sharp_drawable_density!=1.5) break;
    acceptance->sharp_observations++; if(ms>acceptance->max_sharp_ms) acceptance->max_sharp_ms=ms;
    printf("SHARP native_input_sample=%u latest_input_to_publication_ms=%.3f content_revision=%llu drawable_density=1.5 endpoint_dip=%.3f\n",sample,ms,(unsigned long long)p.metrics.last_sharp_content_revision,p.metrics.last_sharp_longest_dip);
  }
  ok=acceptance->sharp_observations==ACCEPTANCE_SAMPLES;
done:
  if(alt) send_alt(false);
  pp_sprites_clear(&p); pp_scene_delete(p.scene,id);
  printf("SHARP native_observations=%u max_ms=%.3f error=\"%s\" compositor_visible=unobserved\n",acceptance->sharp_observations,acceptance->max_sharp_ms,error.message);
  fflush(stdout); return ok;
}
static int native_acceptance(PPApp *app,bool png_only) {
  Acceptance a={.release=!strcmp(PP_PROBE_CONFIGURATION,"Release")};
  PPRenderer *renderer=NULL; PPError error={0}; HANDLE injector=NULL;
  double deadline=pp_probe_clock()+145000,renderer_start=pp_probe_clock();
  pp_probe_completion_enabled=true;
  if(!pp_renderer_create(&renderer,&error)) goto done;
  if(png_only) puts("SCOPE diagnostic=png-retained-only required_cases=16 overall_96_case_acceptance=not-evaluated");
  printf("PROTOCOL build=%s backend=source-dependent-png-retained-others-layered warmup=%u repeated_samples=%u p95=nearest-rank gate=%s rotations=0,17.25 complete_boundary=source-request-through-GPU-completion-and-native-publication compositor_visible=unobserved matrix_poses=synthetic-offscreen\n",PP_PROBE_CONFIGURATION,ACCEPTANCE_WARMUP,ACCEPTANCE_SAMPLES,png_only?"png-only-diagnostic":"default-emoji-1536-dip-150-percent");
  printf("GPU renderer=\"%s\" vendor=\"%s\" version=\"%s\" renderer_cold_first_use_ms=%.3f\n",glGetString(GL_RENDERER),glGetString(GL_VENDOR),glGetString(GL_VERSION),pp_probe_clock()-renderer_start);
  static const char *names[]={"default-emoji","bitmap-noto","png","gif","svg","mixed-text"};
  const double sizes[]={96,384,768,1536},densities[]={1,1.5},angles[]={0,17.25};
  inject_stop=CreateEventW(NULL,TRUE,FALSE,NULL); if(!inject_stop) goto done;
  injection_limit=18000; injector=CreateThread(NULL,0,zero_moves,NULL,0,NULL); if(!injector) goto done;
  for(int kind=png_only?2:0;kind<(png_only?3:6) && !a.stopped && !a.timed_out;kind++) {
    PPItemSnapshot *snapshot=NULL; PPFontInfo font; double source_start=pp_probe_clock();
    if(!acceptance_source(app,kind,&snapshot,&font,&error)) {printf("UNSUPPORTED source=%s error=\"%s\"\n",names[kind],error.message);continue;}
    printf("SOURCE source=%s snapshot_cold_ms=%.3f content_revision=%llu font=\"%s\"\n",names[kind],pp_probe_clock()-source_start,(unsigned long long)pp_item_snapshot_revision(snapshot),font.family);
    if(kind==0) a.default_segoe=!strcmp(font.family,"Segoe UI Emoji");
    for(int density=0;density<2 && !a.stopped && !a.timed_out;density++) for(int size=0;size<4 && !a.stopped && !a.timed_out;size++) for(int angle=0;angle<2 && !a.stopped && !a.timed_out;angle++) {
      PPSurface *surface=NULL; size_t retained=0; double next;
      PPAnchorPresenter p={.renderer=renderer,.assets=pp_app_assets(app)};
      PPSprite sprite={.id=1};
      PPSceneInstance instance={.snapshot=snapshot};
      PPDisplay display={1,{-20000,-20000,10000,10000},densities[density],densities[density]};
      double values[7][ACCEPTANCE_SAMPLES]={{0}}; unsigned observed=0,observed_gpu=0;
      unsigned input_before=pp_probe_injected_hook_count; uint64_t builds_before=pp_assets_source_metrics(p.assets).raster_builds;
      PPRendererMetrics renderer_before=pp_anchor_presenter_renderer_metrics(&p);
      bool direct=kind==2;
      if(!(direct?pp_surface_create_gl(&surface,&error):pp_surface_create(&surface,&error))) {printf("UNSUPPORTED native source=%s error=\"%s\"\n",names[kind],error.message);continue;}
      if(direct) {sprite.surface=surface;p.instances=&sprite;p.instance_count=1;}
      printf("CASE source=%s dip=%.0f drawable_density=%.2f rotation=%.2f backend=%s build=%s\n",names[kind],sizes[size],densities[density],angles[angle],direct?"direct-retained-gl":"layered-gl-readback",PP_PROBE_CONFIGURATION);
      for(int frame=-1;frame<ACCEPTANCE_WARMUP+ACCEPTANCE_SAMPLES;frame++) {
        if(pp_probe_clock()>deadline) {a.timed_out=true;break;}
        pump(); memset(pp_probe_metrics,0,sizeof(pp_probe_metrics)); double start=pp_probe_clock();
        PPPose pose={{-19000+frame*.25,-19000},sizes[size]+fmax(0,frame)*.01,angles[angle],1};
        sprite.pose=pose;instance.pose=pose;
        bool ok=direct?pp_sprite_present(&p,&sprite,&instance,&display,0,&next,&error):
          pp_present_content(&p,surface,snapshot,pose,&display,kind==3?fmax(0,frame)*.04:0,&next,&retained,&error);
        double elapsed=pp_probe_clock()-start; pump();
        if(!ok) {printf("INCOMPLETE presentation error=\"%s\"\n",error.message);break;}
        if(frame==-1) printf("COLD first_presentation_ms=%.3f source_request_ms=%.3f transform_ms=%.3f native_publication_ms=%.3f\n",elapsed,pp_probe_metrics[PROBE_SOURCE_REQUEST].ms,pp_probe_metrics[PROBE_TRANSFORM].ms,pp_probe_metrics[PROBE_SURFACE].ms);
        if(frame>=ACCEPTANCE_WARMUP) {
          values[0][observed]=elapsed; values[1][observed]=pp_probe_metrics[PROBE_SOURCE_REQUEST].ms;
          values[2][observed]=pp_probe_metrics[PROBE_GPU_SUBMIT].ms; values[3][observed]=pp_probe_metrics[PROBE_GPU_EXECUTION].ms;
          values[4][observed]=pp_probe_metrics[PROBE_GPU_COMPLETE].ms; values[5][observed]=pp_probe_metrics[PROBE_SURFACE].ms;
          values[6][observed]=pp_probe_metrics[PROBE_READBACK].ms;
          if(pp_probe_metrics[PROBE_GPU_EXECUTION].count==1 && pp_probe_metrics[PROBE_GPU_COMPLETE].count==1) {a.gpu_observations++;observed_gpu++;}
          observed++; a.samples++;
        }
        if(elapsed>250 || pp_probe_injected_max_wait>100) {a.stopped=true;printf("STOP protective frame_ms=%.3f input_wait_ms=%u\n",elapsed,pp_probe_injected_max_wait);break;}
      }
      double observe_limit=pp_probe_clock()+50;
      while(pp_probe_injected_hook_count==input_before && pp_probe_clock()<observe_limit) {pump();Sleep(1);}
      if(pp_probe_injected_hook_count>input_before) a.native_input_cases++;
      if(observed==ACCEPTANCE_SAMPLES) {
        a.completed_cases++; double p95=statistics("complete",values[0]); a.regression_p95_ms=fmax(a.regression_p95_ms,p95);
        if(kind==0 && size==3 && density==1 && a.default_segoe) {a.required_timing_cases++;a.required_p95_ms=fmax(a.required_p95_ms,p95);}
        statistics("source_request",values[1]); statistics("cpu_draw_submission",values[2]);
        if(observed_gpu==ACCEPTANCE_SAMPLES) statistics("gpu_execution",values[3]);
        else printf("UNAVAILABLE gpu_execution observations=%u/%u\n",observed_gpu,ACCEPTANCE_SAMPLES);
        statistics("cpu_wait_gpu_completion",values[4]);statistics("native_publication",values[5]);statistics("gpu_readback",values[6]);
      }
      PPRendererMetrics renderer_after=pp_anchor_presenter_renderer_metrics(&p);
      printf("OBSERVATIONS phases=cold-warmup-samples warm_samples=%u source_raster_builds=%llu transform_redraws=%llu source_uploads=%llu readbacks=%llu native_publications=%llu injected_hooks=%u\n",observed,
        (unsigned long long)(pp_assets_source_metrics(p.assets).raster_builds-builds_before),
        (unsigned long long)(renderer_after.transform_draws-renderer_before.transform_draws),
        (unsigned long long)(renderer_after.source_uploads-renderer_before.source_uploads),
        (unsigned long long)(renderer_after.readbacks-renderer_before.readbacks),
        (unsigned long long)p.metrics.native_publications,pp_probe_injected_hook_count-input_before);
      if(direct) pp_sprite_release_presentation(&p,&sprite);
      pp_surface_destroy(surface); pp_assets_release_raster(p.assets,retained); fflush(stdout);
    }
    pp_item_snapshot_release(snapshot);
  }
  if(!stop_injector(injector)) {puts("STOP injector did not terminate");a.stopped=true;} else injector=NULL;
  if(!png_only && !a.stopped && !a.timed_out) sharp_native_samples(app,renderer,&a,deadline);
  a.max_input_wait_ms=pp_probe_injected_max_wait;
done:
  if(injector && !stop_injector(injector)) {fflush(stdout);ExitProcess(2);}
  if(inject_stop) {CloseHandle(inject_stop);inject_stop=NULL;}
  pp_renderer_destroy(renderer); pp_probe_completion_enabled=false;
  if(error.code) printf("ACCEPTANCE error=\"%s\"\n",error.message);
  if(png_only) {
    bool complete=!a.stopped&&!a.timed_out&&a.completed_cases==16&&a.samples==16*ACCEPTANCE_SAMPLES&&
      a.gpu_observations==a.samples&&a.native_input_cases==16;
    printf("RESULT png_retained_matrix=%s cases=%u/16 samples=%u/%u gpu_observations=%u native_input_cases=%u regression_worst_p95_ms=%.3f input_max_ms=%.3f overall_spec_gate=not-evaluated compositor_visible=unobserved\n",
      complete?"PASS":"FAIL",a.completed_cases,a.samples,16*ACCEPTANCE_SAMPLES,a.gpu_observations,a.native_input_cases,a.regression_p95_ms,a.max_input_wait_ms);
    return complete?0:1;
  }
  return acceptance_result(&a);
}
int main(int argc,char **argv) {
  if(argc==3 && !strcmp(argv[1],"--acceptance-control")) return acceptance_control(argv[2]);
  static char log_buffer[65536]; setvbuf(stdout,log_buffer,_IOFBF,sizeof(log_buffer));
  PPApp *app=NULL; PPError error={0}; double start=pp_probe_clock(); int result=0;
  CHECK(pp_app_create(&app,&(PPAppOptions){.preferences_directory=""},&error)); report("app_create",pp_probe_clock()-start);
  if(argc>1 && !strcmp(argv[1],"--drawer-cache")) drawer(app,true,false,false);
  else if(argc>1 && !strcmp(argv[1],"--drawer")) drawer(app,false,false,false);
  else if(argc>1 && !strcmp(argv[1],"--drawer-large")) drawer(app,false,true,false);
  else if(argc>1 && !strcmp(argv[1],"--drawer-warm")) drawer(app,false,false,true);
  else if(argc>1 && !strcmp(argv[1],"--drawer-density-first")) drawer_first_layout_density(app);
  else if(argc>1 && !strcmp(argv[1],"--raster-save")) raster_reference(app,true);
  else if(argc>1 && !strcmp(argv[1],"--raster-compare")) raster_reference(app,false);
  else if(argc>1 && !strcmp(argv[1],"--font-source")) font_source();
  else if(argc>1 && !strcmp(argv[1],"--native")) result=native_profile(app)?0:1;
  else if(argc>1 && !strcmp(argv[1],"--acceptance")) result=native_acceptance(app,false);
  else if(argc>1 && !strcmp(argv[1],"--png-retained")) result=native_acceptance(app,true);
  else if(argc>1 && !strcmp(argv[1],"--blur")) blur_profile(app);
  else result=ladder(app,argc>1 && !strcmp(argv[1],"--zoom-150") ? 1.5 : 1,argc>1 && !strcmp(argv[1],"--zoom-budget"))?0:1;
  pp_app_destroy(app); puts(result?"FAIL bounded performance probe":"PASS bounded performance probe"); return result;
}
