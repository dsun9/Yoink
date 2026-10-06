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
PPProbeMetric pp_probe_metrics[PROBE_COUNT];
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
static void report(const char *scenario, double elapsed) {
  static const char *names[] = {"text", "raster", "inspect", "find_font", "renderer_create", "transform", "ui_create", "upload", "ui_end_swap", "surface", "dib", "layered", "readback", "texture"};
  printf("SCENARIO %s wall_ms=%.3f\n", scenario, elapsed);
  for (int i=0;i<PROBE_COUNT;i++) if (pp_probe_metrics[i].count)
    printf("STAGE %s count=%u sum_ms=%.3f max_ms=%.3f\n", names[i], pp_probe_metrics[i].count, pp_probe_metrics[i].ms, pp_probe_metrics[i].max_ms);
  for(unsigned i=0;i<font_count;i++) printf("FONT family=%s found=%d bytes=%zu ms=%.3f\n",font_events[i].family,font_events[i].found,font_events[i].bytes,font_events[i].ms);
  font_count=0;
  memset(pp_probe_metrics, 0, sizeof(pp_probe_metrics)); fflush(stdout);
}
static void ladder(PPApp *app, double density, bool require_budget) {
  const PPProject *project = pp_app_project(app);
  PPItemSnapshot *snapshot = pp_project_snapshot(project, pp_project_target(project, PP_ANCHOR_TOP_LEFT)->id);
  PPVec2 size = pp_item_snapshot_size(snapshot);
  PPRenderer *renderer = NULL; PPError error = {0}; CHECK(pp_renderer_create(&renderer, &error));
  printf("GPU %s density=%.3f canonical=%.0fx%.0f\n", glGetString(GL_RENDERER), density, size.x, size.y);
  memset(pp_probe_metrics,0,sizeof(pp_probe_metrics));
  const double sizes[] = {96,384,768,1536};
  for (int i=0;i<4;i++) {
    double start=pp_probe_clock(), next; const PPFrame *source=NULL; PPFrame final={0};
    CHECK(pp_item_snapshot_frame(snapshot,0,sizes[i]*density/fmax(size.x,size.y),&source,&next,&error));
    CHECK(pp_renderer_transform(renderer,source,sizes[i],0,1,density,&final,&error));
    double elapsed=pp_probe_clock()-start;
    double raster_ms=pp_probe_metrics[PROBE_RASTER].ms;
    printf("ZOOM dip=%.0f source=%dx%d output=%dx%d bytes=%zu\n",sizes[i],source->width,source->height,final.width,final.height,pp_assets_raster_bytes(pp_app_assets(app)));
    pp_frame_destroy(&final); report("zoom",elapsed);
    if(require_budget && sizes[i]==1536) CHECK(raster_ms<100);
    if (elapsed>250) { puts("STOP zoom escalation: frame exceeded 250ms"); break; }
  }
  pp_item_snapshot_release(snapshot); pp_renderer_destroy(renderer);
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
static DWORD WINAPI zero_moves(void *unused) {
  (void)unused;
  for(int i=0;i<250 && WaitForSingleObject(inject_stop,10)==WAIT_TIMEOUT;i++) {
    INPUT input={0}; input.type=INPUT_MOUSE; input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_MOVE_NOCOALESCE;
    input.mi.dx=(i&1)?-1:1; /* Bounded alternating motion; supervisor restores cursor. */
    if(SendInput(1,&input,sizeof(input))==1) InterlockedIncrement(&injected_moves);
  }
  return 0;
}
static void pump(void) {PPPlatformEvent event;while(pp_platform_poll(&event)) {}}
static void native_profile(PPApp *app) {
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
}
int main(int argc,char **argv) {
  static char log_buffer[65536]; setvbuf(stdout,log_buffer,_IOFBF,sizeof(log_buffer));
  PPApp *app=NULL; PPError error={0}; double start=pp_probe_clock();
  CHECK(pp_app_create(&app,&(PPAppOptions){.preferences_directory=""},&error)); report("app_create",pp_probe_clock()-start);
  if(argc>1 && !strcmp(argv[1],"--drawer-cache")) drawer(app,true,false,false);
  else if(argc>1 && !strcmp(argv[1],"--drawer")) drawer(app,false,false,false);
  else if(argc>1 && !strcmp(argv[1],"--drawer-large")) drawer(app,false,true,false);
  else if(argc>1 && !strcmp(argv[1],"--drawer-warm")) drawer(app,false,false,true);
  else if(argc>1 && !strcmp(argv[1],"--raster-save")) raster_reference(app,true);
  else if(argc>1 && !strcmp(argv[1],"--raster-compare")) raster_reference(app,false);
  else if(argc>1 && !strcmp(argv[1],"--font-source")) font_source();
  else if(argc>1 && !strcmp(argv[1],"--native")) native_profile(app);
  else if(argc>1 && !strcmp(argv[1],"--blur")) blur_profile(app);
  else ladder(app,argc>1 && !strcmp(argv[1],"--zoom-150") ? 1.5 : 1,argc>1 && !strcmp(argv[1],"--zoom-budget"));
  pp_app_destroy(app); puts("PASS bounded performance probe"); return 0;
}
