#include "../../src/app/anchors.h"
#include "../../src/app/presenter_internal.h"
#include "../../src/app/runtime_internal.h"
#include "../../src/core/geometry_internal.h"
#include "../../src/ui/drawer_internal.h"
#include "../project_fixture.h"
#include "../test.h"
size_t pp_surface_raster_bytes(int width,int height) {return (size_t)width*height*4;}
#include "pointy/runtime.h"
#include <math.h>
#include <SDL3/SDL.h>
struct PPSurface {
  uint64_t id;
  PPRect bounds;
  bool shown;
  struct PPSurface *next;
  int source_width;
  uint8_t red, blue;
  uint32_t window_id;
};
struct PPRenderer {PPSurface *surface;PPRendererMetrics metrics;uint64_t content_id,revision;};
struct PPSharedTexture {const PPFrame *frame;};
static uint32_t direct_window_serial;
static unsigned cpu_transforms;
static PPSurface *surfaces;
static int source_width;
static PPSurface *fail_surface;
static unsigned capture_releases;
static PPSurface *capture_owner;
bool pp_surface_create(PPSurface **out, PPError *error) {
  (void)error;
  *out = calloc(1, sizeof(**out));
  CHECK(*out);
  (*out)->next = surfaces;
  surfaces = *out;
  return true;
}
bool pp_surface_present(PPSurface *s, const PPFrame *frame, PPRect bounds,
                        PPError *error) {
  (void)error;
  if (s == fail_surface) {
    if (error)
      *error = (PPError){PP_ERROR_PLATFORM, "Injected presentation failure"};
    return false;
  }
  CHECK(bounds.w == frame->width);
  s->bounds = bounds;
  s->shown = true;
  s->source_width = source_width;
  s->red = frame->pixels[0];
  s->blue = frame->pixels[2];
  return true;
}
void pp_surface_destroy(PPSurface *s) {
  if (!s)
    return;
  if (capture_owner == s)
    capture_owner = NULL;
  PPSurface **link = &surfaces;
  while (*link != s)
    link = &(*link)->next;
  *link = s->next;
  free(s);
}
void pp_surface_hide(PPSurface *s) { s->shown = false;if(capture_owner==s)capture_owner=NULL; }
void pp_surface_set_instance_id(PPSurface *s, uint64_t id) { s->id = id; }
bool pp_platform_capture(PPSurface *s, PPError *error) {
  (void)error;
  capture_owner = s;
  return true;
}
void pp_platform_release_capture(void) {
  capture_releases++;
  capture_owner = NULL;
}
void pp_surface_cancel_input(PPSurface *s) {
  if (capture_owner == s)
    pp_platform_release_capture();
}
void pp_surface_raise(PPSurface *s) { (void)s; }
bool pp_platform_poll(PPPlatformEvent *out) {
  (void)out;
  return false;
}
static PPVec2 pointer;
bool pp_platform_pointer(PPVec2 *out) {
  *out = pointer;
  return true;
}
static PPDisplay displays[2] = {{1, {0, 0, 1920, 1080}, 1, 1},
                                {2, {1920, 0, 1920, 1080}, 2, 2}};
static size_t display_count = 2;
bool pp_platform_displays(PPDisplay **out, size_t *count, PPError *error) {
  (void)error;
  *out = malloc(sizeof(displays));
  memcpy(*out, displays, sizeof(displays));
  *count = display_count;
  return true;
}
void pp_displays_free(PPDisplay *d) { free(d); }
void pp_platform_gesture_config(PPGestureConfig *out) {
  *out = (PPGestureConfig){2, 2, 2, 2, .5, 10, 1.1, .12};
}
bool pp_renderer_create(PPRenderer **out, PPError *error) {
  (void)error;
  *out = (PPRenderer *)1;
  return true;
}
void pp_renderer_destroy(PPRenderer *r) {if(r && r!=(PPRenderer *)1)free(r);}
bool pp_renderer_create_for_ui(PPRenderer **out,uint32_t id,void *context,PPError *error) {
  (void)out;(void)id;(void)context;(void)error;CHECK(false);return false;
}
bool pp_renderer_preview_source(PPRenderer *r,const PPFrame *source,uint64_t id,uint64_t revision,
    PPVec2 layout,PPRect rect,double longest,double angle,double opacity,double density,
    unsigned *texture,int *width,int *height,PPError *error) {
  (void)r;(void)source;(void)id;(void)revision;(void)layout;(void)rect;(void)longest;
  (void)angle;(void)opacity;(void)density;(void)texture;(void)width;(void)height;(void)error;
  CHECK(false);return false;
}
PPRendererMetrics pp_renderer_metrics(const PPRenderer *r) {return r==(PPRenderer *)1?(PPRendererMetrics){cpu_transforms,cpu_transforms,cpu_transforms*2}:r?r->metrics:(PPRendererMetrics){0};}
bool pp_surface_create_gl(PPSurface **out,PPError *error) {if(!pp_surface_create(out,error))return false;(*out)->window_id=++direct_window_serial;return true;}
uint32_t pp_surface_window_id(const PPSurface *s) {return s?s->window_id:0;}
bool pp_surface_place_gl(PPSurface *s,PPRect b,bool interactive,PPError *error) {(void)error;s->bounds=b;s->shown=interactive;if(!interactive && capture_owner==s)capture_owner=NULL;return true;}
bool pp_surface_set_grab_region(PPSurface *s,const PPGrabRegion *r,PPError *error) {(void)s;(void)r;(void)error;return true;}
void pp_surface_set_rectangular(PPSurface *s,bool enabled) {(void)s;(void)enabled;}
void pp_surface_inherit_order(PPSurface *s,PPSurface *old) {(void)s;(void)old;}
bool pp_renderer_create_for_window(PPRenderer **out,uint32_t id,PPError *error) {
    (void)error;*out=calloc(1,sizeof(**out));CHECK(*out);
    for(PPSurface *s=surfaces;s;s=s->next)if(s->window_id==id)(*out)->surface=s;
    CHECK((*out)->surface);return true;
}
bool pp_renderer_create_texture_owner(PPRenderer **out,PPError *error) {(void)error;*out=calloc(1,sizeof(**out));return *out!=NULL;}
bool pp_renderer_create_shared_window(PPRenderer **out,uint32_t id,PPRenderer *owner,PPError *error) {(void)owner;return pp_renderer_create_for_window(out,id,error);}
bool pp_shared_texture_create(PPRenderer *owner,const PPFrame *frame,PPSharedTexture **out,PPError *error) {
    (void)error;*out=malloc(sizeof(**out));CHECK(*out);(*out)->frame=frame;owner->metrics.source_uploads++;return true;
}
bool pp_renderer_bind_shared(PPRenderer *r,PPSharedTexture *texture,uint64_t id,uint64_t revision,PPError *error) {
    (void)error;r->content_id=id;r->revision=revision;r->surface->source_width=texture->frame->width;
    r->surface->red=texture->frame->pixels[0];r->surface->blue=texture->frame->pixels[2];return true;
}
void pp_shared_texture_destroy(PPSharedTexture *texture){free(texture);}
bool pp_renderer_upload_source(PPRenderer *r,const PPFrame *frame,uint64_t id,uint64_t revision,PPError *error) {
    (void)error;if(r->content_id==id&&r->revision==revision)return true;
    r->surface->source_width=frame->width;r->surface->red=frame->pixels[0];r->surface->blue=frame->pixels[2];
    for(int y=0;y<frame->height;y++)for(int x=0;x<frame->width;x++) {
      const uint8_t *pixel=frame->pixels+(size_t)y*frame->stride+x*4;
      if(pixel[3]==255){r->surface->red=pixel[0];r->surface->blue=pixel[2];y=frame->height;break;}
    }
    r->content_id=id;r->revision=revision;r->metrics.source_uploads++;return true;
}
bool pp_renderer_draw(PPRenderer *r,PPVec2 layout,double size,double angle,double opacity,double density,PPError *error) {
    (void)layout;(void)size;(void)angle;(void)opacity;(void)density;
    if(r->surface==fail_surface){if(error)*error=(PPError){PP_ERROR_RENDER,"Injected direct presentation failure"};return false;}
    r->metrics.transform_draws++;return true;
}
bool pp_renderer_draw_source(PPRenderer *r,PPVec2 layout,PPRect source_rect,double size,double angle,double opacity,double density,PPError *error) {
  (void)source_rect;return pp_renderer_draw(r,layout,size,angle,opacity,density,error);
}
bool pp_renderer_draw_source_at(PPRenderer *r,PPVec2 layout,PPRect source_rect,double size,double angle,double opacity,double density,PPVec2 center,PPError *error) {
  (void)center;return pp_renderer_draw_source(r,layout,source_rect,size,angle,opacity,density,error);
}
bool pp_renderer_supports_fractional_placement(void) {return false;}
bool pp_renderer_transform(PPRenderer *r, const PPFrame *source, double longest,
                           double angle, double opacity, double scale,
                           PPFrame *out, PPError *error) {
  cpu_transforms++;
  (void)r;
  (void)opacity;
  (void)error;
  int w, h;
  CHECK(pp_geometry_extent(source, longest, angle, scale, &w, &h));
  *out = (PPFrame){calloc((size_t)w * h, 4), calloc((size_t)w * h, 1), w, h,
                   (size_t)w * 4};
  source_width = source->width;
  for (int y = 0; y < source->height; y++)
    for (int x = 0; x < source->width; x++) {
      const uint8_t *pixel =
          source->pixels + (size_t)y * source->stride + x * 4;
      if (pixel[3] == 255) {
        out->pixels[0] = pixel[0];
        out->pixels[2] = pixel[2];
        return true;
      }
    }
  return true;
}
void pp_frame_destroy(PPFrame *f) {
  free(f->pixels);
  free(f->content_alpha);
  *f = (PPFrame){0};
}
bool pp_renderer_transform_source(PPRenderer *r,const PPFrame *source,PPVec2 layout,PPRect rect,double longest,double angle,double opacity,double scale,PPFrame *out,PPError *error) {
  (void)r;(void)opacity;(void)error;cpu_transforms++;int w,h;CHECK(pp_geometry_source_extent(layout,rect,longest,angle,scale,&w,&h));
  *out=(PPFrame){calloc((size_t)w*h,4),calloc((size_t)w*h,1),w,h,(size_t)w*4};source_width=source->width;
  for(int y=0;y<source->height;y++)for(int x=0;x<source->width;x++) {
    const uint8_t *pixel=source->pixels+(size_t)y*source->stride+x*4;
    if(pixel[3]==255){out->pixels[0]=pixel[0];out->pixels[2]=pixel[2];return true;}
  }
  return true;
}
bool pp_renderer_transform_color_source(PPRenderer *r,const PPFrame *source,PPVec2 layout,PPRect rect,double longest,double angle,double opacity,double scale,PPFrame *out,PPError *error) {
  bool ok=pp_renderer_transform_source(r,source,layout,rect,longest,angle,opacity,scale,out,error);
  free(out->content_alpha);out->content_alpha=NULL;return ok;
}

static void step(PPRuntime *r, PPSurface *s, PPPlatformEventKind kind, double x,
                 double y, double t) {
  pointer = (PPVec2){x, y};
  pp_runtime_event(r, &(PPPlatformEvent){.surface = s,
                                         .instance_id = s ? s->id : UINT64_MAX,
                                         .kind = kind,
                                         .desktop_point = {x, y},
                                         .button = PP_BUTTON_LEFT,
                                         .timestamp = t});
}
static PPSurface *sprite(uint64_t id) {
  for (PPSurface *s = surfaces; s; s = s->next)
    if (s->id == id)
      return s;
  return NULL;
}
static void settle_worker(PPRuntime *runtime,double now) {
  unsigned idle=0;
  for(int i=0;i<5000&&idle<2;i++) {
    SDL_Delay(1);pp_runtime_tick(runtime,now);
    idle=pp_runtime_presentation_metrics(runtime).worker_pending_jobs?0:idle+1;
  }
  CHECK(idle==2);
}
static uint64_t pull(PPRuntime *r, double now) {
  pointer = (PPVec2){8, 8};
  pp_runtime_tick(r, now);
  PPSurface *preview = NULL;
  for (PPSurface *s = surfaces; s; s = s->next)
    if (!s->id && s->shown && s->bounds.w > 16 && s->bounds.x == 0)
      preview = s;
  CHECK(preview);
  size_t before = pp_scene_count(pp_app_scene(r->app));
  step(r, preview, PP_EVENT_BUTTON_DOWN, 40, 40, now + .01);
  step(r, preview, PP_EVENT_POINTER_MOVE, 41, 41, now + .02);
  CHECK(pp_scene_count(pp_app_scene(r->app)) == before);
  step(r, preview, PP_EVENT_POINTER_MOVE, 300, 200, now + .03);
  step(r, preview, PP_EVENT_POINTER_MOVE, 320, 210, now + .04);
  CHECK(pp_scene_count(pp_app_scene(r->app)) == before + 1);
  step(r, preview, PP_EVENT_BUTTON_UP, 320, 210, now + .05);
  pp_runtime_tick(r, now + .06);
  return pp_scene_at(pp_app_scene(r->app), before)->id;
}
static void test_runtime_ownership(void) {
  PPApp *app = NULL;
  PPRuntime *r = NULL;
  PPError error = {0};
  CHECK(pp_app_create(&app, &(PPAppOptions){.preferences_directory = ""},
                      &error));
  PPProject *arrows = test_arrow_project(pp_app_assets(app));
  PPItem arrow = *pp_project_target(arrows, PP_ANCHOR_TOP_LEFT);
  arrow.id = pp_project_target(pp_app_project(app), PP_ANCHOR_TOP_LEFT)->id;
  CHECK(pp_app_command(app,
                       &(PPCommand){.kind = PP_COMMAND_EDIT_ITEM,
                                    .item_id = arrow.id,
                                    .item = &arrow},
                       &error));
  pp_project_destroy(arrows);
  CHECK(pp_runtime_create(&r, app, &error));
  pointer = (PPVec2){8, 8};
  pp_runtime_tick(r, .1);
  PPSurface *preview = NULL;
  for (PPSurface *candidate = surfaces; candidate; candidate = candidate->next)
    if (!candidate->id && candidate->shown && candidate->bounds.w > 16 &&
        candidate->bounds.x == 0)
      preview = candidate;
  CHECK(preview);
  step(r, preview, PP_EVENT_BUTTON_DOWN, 40, 40, .2);
  step(r, preview, PP_EVENT_POINTER_MOVE, 300, 200, .3);
  CHECK(pp_scene_count(pp_app_scene(app)) == 1);
  uint64_t removed = pp_scene_at(pp_app_scene(app), 0)->id;
  pp_runtime_event(r, &(PPPlatformEvent){.surface = sprite(removed),
                                         .instance_id = removed,
                                         .kind = PP_EVENT_BUTTON_DOWN,
                                         .button = PP_BUTTON_RIGHT,
                                         .timestamp = .4});
  CHECK(pp_scene_count(pp_app_scene(app)) == 0);
  /* Moves already queued for the source cannot spawn a second copy after
   *
   * the in-progress instance was removed. A new press is required. */
  step(r, preview, PP_EVENT_POINTER_MOVE, 320, 210, .5);
  step(r, preview, PP_EVENT_BUTTON_UP, 320, 210, .6);
  CHECK(pp_scene_count(pp_app_scene(app)) == 0);
  uint64_t a = pull(r, 1), b = pull(r, 2);
  CHECK(a != b && pp_scene_count(pp_app_scene(app)) == 2);
  CHECK(pp_scene_get(pp_app_scene(app), a)->snapshot ==
        pp_scene_get(pp_app_scene(app), b)->snapshot);
  PPPose external = pp_scene_get(pp_app_scene(app), a)->pose;
  external.center = (PPVec2){440, 330};
  external.opacity = .7;
  CHECK(pp_scene_set_pose(pp_app_scene(app), a, external, &error));
  pp_runtime_tick(r, 2.5);
  CHECK(pp_scene_get(pp_app_scene(app), a)->pose.center.x == 440 &&
        sprite(a)->bounds.x == 392);
  const char *wide = "<svg width='160' height='40'><path fill='red' "
                     "d='M0 0H160V40H0Z'/></svg>";
  PPItem replacement = {
      .kind = PP_ITEM_IMAGE, .image_longest_dip = 80, .opacity = 1};
  CHECK(pp_asset_import(pp_app_assets(app),
                        (PPBytes){(const uint8_t *)wide, strlen(wide)},
                        PP_ASSET_SVG, &replacement.image_asset, &error));
  CHECK(pp_app_command(app,
                       &(PPCommand){.kind = PP_COMMAND_ADD_ITEM,
                                    .anchor = PP_ANCHOR_TOP_LEFT,
                                    .item = &replacement},
                       &error));
  replacement.id = pp_app_ui(app)->selected_item;
  uint64_t revision = pp_project_revision(pp_app_project(app));
  PPSurface *s = sprite(a);
  CHECK(s);
  PPPlatformEvent wheel = {.surface = s,
                           .instance_id = a,
                           .kind = PP_EVENT_WHEEL,
                           .wheel_delta = 1,
                           .timestamp = 3};
  pp_runtime_event(r, &wheel);
  pp_runtime_tick(r, 3.12);
  CHECK(fabs(pp_scene_get(pp_app_scene(app), a)->pose.angle_deg - 10) < 1e-8);
  wheel.modifiers = PP_MOD_ALT;
  wheel.timestamp = 4;
  pp_runtime_event(r, &wheel);
  pp_runtime_tick(r, 4.12);
  CHECK(fabs(pp_scene_get(pp_app_scene(app), a)->pose.longest_dip - 105.6) <
        1e-8);
  PPPose saved = pp_scene_get(pp_app_scene(app), a)->pose;
  step(r, s, PP_EVENT_BUTTON_DOWN, 300, 200, 5);
  step(r, s, PP_EVENT_BUTTON_UP, 300, 200, 5.01);
  step(r, s, PP_EVENT_BUTTON_DOWN, 300, 200, 5.1);
  step(r, s, PP_EVENT_BUTTON_UP, 300, 200, 5.11);
  CHECK(r->drawer && r->drawer->instance_id == a);
  CHECK(pp_drawer_select(r->drawer, replacement.id, 6, &error));
  CHECK(pp_project_revision(pp_app_project(app)) == revision);
  const PPSceneInstance *instance = pp_scene_get(pp_app_scene(app), a);
  CHECK(!memcmp(&saved, &instance->pose, sizeof(saved)) &&
        instance->animation_start == 6);
  CHECK(instance->origin_anchor == PP_ANCHOR_TOP_LEFT);
  CHECK(pp_item_snapshot_item(instance->snapshot)->id == replacement.id);
  PPVec2 new_size = pp_item_snapshot_size(instance->snapshot);
  CHECK(new_size.x == 4 * new_size.y);
  CHECK(
      pp_project_anchor(pp_app_project(app), PP_ANCHOR_TOP_LEFT)->default_id ==
      arrow.id);
  pp_runtime_tick(r, 6.01);
  settle_worker(r,6.01);
  CHECK(sprite(a)->red == 255 && sprite(a)->blue == 0);
  pp_drawer_close(r->drawer);
  r->drawer = NULL;
  CHECK(pp_app_command(app,
                       &(PPCommand){.kind = PP_COMMAND_REMOVE_ITEM,
                                    .anchor = PP_ANCHOR_TOP_LEFT,
                                    .item_id = arrow.id},
                       &error));
  CHECK(pp_app_command(app,
                       &(PPCommand){.kind = PP_COMMAND_REMOVE_ITEM,
                                    .anchor = PP_ANCHOR_TOP_LEFT,
                                    .item_id = replacement.id},
                       &error));
  CHECK(pp_drawer_open(&r->drawer, app, a, &error));
  CHECK(pp_drawer_count(r->drawer) == 0);
  CHECK(
      pp_item_snapshot_item(pp_scene_get(pp_app_scene(app), a)->snapshot)->id ==
      replacement.id);
  CHECK(!pp_drawer_select(r->drawer, replacement.id, 7, &error));
  pp_drawer_close(r->drawer);
  r->drawer = NULL;
  pp_runtime_event(r, &(PPPlatformEvent){.surface = s,
                                         .instance_id = a,
                                         .kind = PP_EVENT_BUTTON_DOWN,
                                         .button = PP_BUTTON_RIGHT,
                                         .timestamp = 7});
  CHECK(!pp_scene_get(pp_app_scene(app), a) && !sprite(a));
  CHECK(capture_releases > 0);
  step(r, NULL, PP_EVENT_CANCEL, 0, 0, 8);
  CHECK(pp_scene_count(pp_app_scene(app)) == 1);
  CHECK(pp_app_command(
      app,
      &(PPCommand){.kind = PP_COMMAND_OPEN, .path = "missing-task9.pointy"},
      &error));
  CHECK(!pp_app_command(app,
                        &(PPCommand){.kind = PP_COMMAND_RESOLVE_TRANSITION,
                                     .choice = PP_UNSAVED_DISCARD},
                        &error));
  pp_runtime_tick(r, 9);
  CHECK(pp_scene_count(pp_app_scene(app)) == 1 && sprite(b));
  CHECK(pp_app_command(app, &(PPCommand){.kind = PP_COMMAND_NEW}, &error));
  CHECK(pp_app_command(app,
                       &(PPCommand){.kind = PP_COMMAND_RESOLVE_TRANSITION,
                                    .choice = PP_UNSAVED_DISCARD},
                       &error));
  pp_runtime_event(r, &(PPPlatformEvent){.surface = sprite(b),
                                         .instance_id = b,
                                         .kind = PP_EVENT_WHEEL,
                                         .wheel_delta = 1,
                                         .timestamp = 10});
  CHECK(!surfaces && pp_scene_count(pp_app_scene(app)) == 0);
  pp_runtime_destroy(r);
  CHECK(!surfaces);
  pp_app_destroy(app);
}

static void test_failure_isolation(void) {
  PPApp *app = NULL;
  PPRuntime *r = NULL;
  PPError error = {0};
  CHECK(pp_app_create(&app, &(PPAppOptions){.preferences_directory = ""},
                      &error));
  PPProject *arrows = test_arrow_project(pp_app_assets(app));
  PPItem arrow = *pp_project_target(arrows, PP_ANCHOR_TOP_LEFT);
  arrow.id = pp_project_target(pp_app_project(app), PP_ANCHOR_TOP_LEFT)->id;
  CHECK(pp_app_command(app,
                       &(PPCommand){.kind = PP_COMMAND_EDIT_ITEM,
                                    .item_id = arrow.id,
                                    .item = &arrow},
                       &error));
  pp_project_destroy(arrows);
  CHECK(pp_runtime_create(&r, app, &error));
  uint64_t a = pull(r, 20), b = pull(r, 21);
  settle_worker(r,21.1);
  PPSurface *sa = sprite(a), *sb = sprite(b);
  PPPose before = pp_scene_get(pp_app_scene(app), a)->pose;
  PPRect bitmap = sa->bounds;
  /* Force a candidate budget rejection, without failing native upload. The
   * previous bitmap and scene pose must survive and the target must stop. */
  size_t held = 0;
  const size_t chunk = 1024 * 1024;
  while (pp_assets_reserve_raster(pp_app_assets(app), chunk, NULL))
    held += chunk;
  pp_runtime_event(r, &(PPPlatformEvent){.surface = sa,
                                         .instance_id = a,
                                         .kind = PP_EVENT_WHEEL,
                                         .modifiers = PP_MOD_ALT,
                                         .wheel_delta = 30,
                                         .timestamp = 22});
  pp_runtime_tick(r, 22.2);
  CHECK(!r->failed && sprite(a) == sa && sprite(b) == sb);
  CHECK(!memcmp(&pp_scene_get(pp_app_scene(app), a)->pose, &before,
                sizeof(before)));
  CHECK(!memcmp(&sa->bounds, &bitmap, sizeof(bitmap)) &&
        *pp_app_diagnostic(app));
  pp_assets_release_raster(pp_app_assets(app), held);
  pp_runtime_tick(r, 23);
  CHECK(!memcmp(&pp_scene_get(pp_app_scene(app), a)->pose, &before,
                sizeof(before)));
  /* The 96 x 96 arrow endpoint has one RGBA + alpha charge shared by equal
   * consumers. Compatibility windows also retain a private RGBA native copy. */
  PPContentSources *sources=pp_app_content_sources(app,&error);
  CHECK(pp_content_sources_raster_bytes(sources)==46080);
  PPItemSnapshot *snapshot=pp_scene_get(pp_app_scene(app),b)->snapshot;
  const PPFrame *canonical=NULL;double next;
  CHECK(pp_item_snapshot_frame(snapshot,0,1,&canonical,&next,&error));
  size_t canonical_bytes=canonical->stride*(size_t)canonical->height;
  uint8_t *canonical_pixels=malloc(canonical_bytes);CHECK(canonical_pixels);
  memcpy(canonical_pixels,canonical->pixels,canonical_bytes);
  size_t before_observer=pp_assets_external_raster_bytes(pp_app_assets(app));
  PPContentConsumer *observer=NULL;PPContentFrame shared={0};
  CHECK(pp_content_consumer_create(sources,&observer,&error));
  CHECK(pp_content_frame(observer,snapshot,96,1,0,&shared,&next,&error));
  pp_content_accept(observer);
  CHECK(shared.frame&&shared.frame->width==96&&shared.frame->height==96);
  CHECK(pp_content_sources_raster_bytes(sources)==46080);
  CHECK(pp_assets_external_raster_bytes(pp_app_assets(app))==before_observer);
  const PPFrame *shared_frame=shared.frame;
  uint8_t shared_pixels[36864];memcpy(shared_pixels,shared.frame->pixels,sizeof(shared_pixels));
  uint8_t shared_alpha[9216];memcpy(shared_alpha,shared.frame->content_alpha,sizeof(shared_alpha));
  CHECK(sa->bounds.w==96&&sa->bounds.h==96);
  CHECK(sb->red==255&&sb->blue==0);
  PPRect sibling_bounds=sb->bounds;
  step(r,sb,PP_EVENT_BUTTON_DOWN,320,210,23.1);
  CHECK(capture_owner==sb);
  unsigned releases=capture_releases;
  size_t charged = pp_assets_external_raster_bytes(pp_app_assets(app));
  size_t gpu_charged=pp_runtime_gpu_resources(r).retained_bytes;
  bool was_direct=sa->window_id!=0;
  fail_surface = sa;
  pp_runtime_event(r, &(PPPlatformEvent){.surface = sa,
                                         .instance_id = a,
                                         .kind = PP_EVENT_WHEEL,
                                         .wheel_delta = 1,
                                         .timestamp = 24});
  pp_runtime_tick(r, 24.2);
  fail_surface = NULL;
  CHECK(!r->failed && !sprite(a) && sprite(b) == sb);
  CHECK(!pp_scene_get(pp_app_scene(app), a) &&
        pp_scene_get(pp_app_scene(app), b));
  CHECK(pp_assets_external_raster_bytes(pp_app_assets(app))==charged-(was_direct?0:36864));
  CHECK(pp_runtime_gpu_resources(r).retained_bytes==gpu_charged-(was_direct?36864:0));
  CHECK(pp_content_sources_raster_bytes(sources)==46080);
  CHECK(pp_content_frame(observer,snapshot,96,1,0,&shared,&next,&error));
  CHECK(shared.frame==shared_frame);
  CHECK(!memcmp(shared.frame->pixels,shared_pixels,sizeof(shared_pixels)));
  CHECK(!memcmp(shared.frame->content_alpha,shared_alpha,sizeof(shared_alpha)));
  const PPFrame *after_canonical=NULL;
  CHECK(pp_item_snapshot_frame(snapshot,0,1,&after_canonical,&next,&error));
  CHECK(after_canonical==canonical&&!memcmp(after_canonical->pixels,canonical_pixels,canonical_bytes));
  free(canonical_pixels);
  CHECK(sb->red==255&&sb->blue==0&&!memcmp(&sb->bounds,&sibling_bounds,sizeof(sibling_bounds)));
  CHECK(capture_owner==sb&&capture_releases==releases);
  CHECK(strstr(pp_app_diagnostic(app), "presentation failure"));
  step(r,sb,PP_EVENT_BUTTON_UP,320,210,24.3);CHECK(!capture_owner);
  pointer = (PPVec2){8, 8};
  pp_runtime_tick(r, 25);
  for (PPSurface *preview = surfaces; preview; preview = preview->next)
    if (!preview->id && preview->shown && preview->bounds.x == 0 &&
        preview->bounds.w > 16) {
      fail_surface = preview;
      break;
    }
  CHECK(fail_surface);
  pointer = (PPVec2){500, 500};
  pp_runtime_tick(r, 25.01);
  pp_runtime_tick(r, 25.02);
  fail_surface = NULL;
  CHECK(!r->failed && sprite(b) == sb && pp_scene_get(pp_app_scene(app), b));
  pp_content_consumer_destroy(observer);
  pp_runtime_destroy(r);
  CHECK(!surfaces);
  CHECK(!pp_content_sources_raster_bytes(sources)&&!pp_content_gpu_resources(sources).retained_bytes);
  pp_scene_clear(pp_app_scene(app));
  for(int anchor=0;anchor<PP_ANCHOR_COUNT;anchor++) {
    const PPAnchorList *list=pp_project_anchor(pp_app_project(app),(PPAnchor)anchor);
    while(list->count) {
      CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_REMOVE_ITEM,
        .anchor=(PPAnchor)anchor,.item_id=list->default_id},&error));
      list=pp_project_anchor(pp_app_project(app),(PPAnchor)anchor);
    }
  }
  CHECK(!pp_assets_external_raster_bytes(pp_app_assets(app)));
  pp_app_destroy(app);
}
static void test_external_change_cancels_owned_input(void) {
  PPApp *app = NULL;
  PPRuntime *r = NULL;
  PPError error = {0};
  CHECK(pp_app_create(&app, &(PPAppOptions){.preferences_directory = ""},
                      &error));
  CHECK(pp_runtime_create(&r, app, &error));
  uint64_t a = pull(r, 30), b = pull(r, 31);
  for (int content = 0; content < 2; content++)
    for (int drag = 0; drag < 2; drag++) {
      double t = 32 + 4 * (2 * content + drag);
      PPSurface *sa = sprite(a), *sb = sprite(b);
      step(r, sa, PP_EVENT_BUTTON_DOWN, 320, 210, t);
      if (drag)
        step(r, sa, PP_EVENT_POINTER_MOVE, 500, 400, t + .01);
      PPSprite *mapped = pp_sprites_find(r->presenter, a);
      CHECK(capture_owner == sa && mapped->interaction.pressed);
      CHECK(mapped->dragging == (bool)drag);
      if (content) {
        PPItem replacement =
            *pp_project_target(pp_app_project(app), PP_ANCHOR_TOP_LEFT);
        /* The bundled emoji fallback also covers hosts without Aptos. */
        replacement.text = "\xf0\x9f\x91\x88";
        CHECK(pp_scene_replace(pp_app_scene(app), a, &replacement, t + .02,
                               &error));
      } else {
        PPPose pose = pp_scene_get(pp_app_scene(app), a)->pose;
        pose.center.x += 20;
        CHECK(pp_scene_set_pose(pp_app_scene(app), a, pose, &error));
      }
      pp_runtime_tick(r, t + .03);
      CHECK(!capture_owner);
      CHECK(!mapped->interaction.pressed && !mapped->interaction.dragging &&
            !mapped->dragging);
      /* Old queued trajectory cannot move the newly synchronized instance. */
      PPPose changed = pp_scene_get(pp_app_scene(app), a)->pose;
      step(r, sa, PP_EVENT_POINTER_MOVE, 1000, 800, t + .04);
      CHECK(!memcmp(&changed, &pp_scene_get(pp_app_scene(app), a)->pose,
                    sizeof(changed)));
      /* Synchronizing A must not release B's accepted press or drag. */
      step(r, sb, PP_EVENT_BUTTON_DOWN, 320, 210, t + 1);
      if (drag)
        step(r, sb, PP_EVENT_POINTER_MOVE, 550, 440, t + 1.01);
      CHECK(capture_owner == sb);
      changed.center.y += 20;
      CHECK(pp_scene_set_pose(pp_app_scene(app), a, changed, &error));
      pp_runtime_tick(r, t + 1.02);
      CHECK(capture_owner == sb &&
            pp_sprites_find(r->presenter, b)->interaction.pressed);
      step(r, sb, PP_EVENT_BUTTON_UP, 550, 440, t + 1.03);
      CHECK(!capture_owner);
    }
  pp_runtime_destroy(r);
  CHECK(!surfaces && !capture_owner);
  pp_app_destroy(app);
  puts("PASS external pose/content changes cancel accepted press and drag "
       "ownership; unrelated ownership survives");
}
static void test_png_runtime_reuse_and_generation_cleanup(void) {
  PPApp *app=NULL;PPRuntime *r=NULL;PPError error={0};
  CHECK(pp_app_create(&app,&(PPAppOptions){.preferences_directory=""},&error));
  FILE *file=fopen(PP_MEDIA_FIXTURES "/alpha.png","rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));long size=ftell(file);CHECK(size>0);rewind(file);
  uint8_t *data=malloc((size_t)size);CHECK(data);CHECK(fread(data,1,(size_t)size,file)==(size_t)size);fclose(file);
  PPItem item={.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=1,.id=pp_project_target(pp_app_project(app),PP_ANCHOR_TOP_LEFT)->id};
  CHECK(pp_asset_import(pp_app_assets(app),(PPBytes){data,(size_t)size},PP_ASSET_PNG,&item.image_asset,&error));free(data);
  CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_EDIT_ITEM,.item_id=item.id,.item=&item},&error));
  CHECK(pp_runtime_create(&r,app,&error));pointer=(PPVec2){8,8};pp_runtime_tick(r,1);
  PPSurface *preview=NULL;for(PPSurface *s=surfaces;s;s=s->next)if(!s->id&&s->shown&&s->source_width==3)preview=s;CHECK(preview);
  step(r,preview,PP_EVENT_BUTTON_DOWN,16,16,1.01);step(r,preview,PP_EVENT_POINTER_MOVE,300,200,1.02);step(r,preview,PP_EVENT_BUTTON_UP,300,200,1.03);pp_runtime_tick(r,1.3);
  CHECK(pp_scene_count(pp_app_scene(app))==1);uint64_t id=pp_scene_at(pp_app_scene(app),0)->id;PPSurface *surface=sprite(id);CHECK(surface&&surface->window_id);
  PPRendererMetrics warm=pp_runtime_renderer_metrics(r);PPSourceMetrics sources=pp_assets_source_metrics(pp_app_assets(app));
  pp_runtime_tick(r,2);CHECK(pp_runtime_renderer_metrics(r).transform_draws==warm.transform_draws);
  step(r,surface,PP_EVENT_BUTTON_DOWN,300,200,2.01);step(r,surface,PP_EVENT_POINTER_MOVE,400,300,2.02);step(r,surface,PP_EVENT_BUTTON_UP,400,300,2.03);pp_runtime_tick(r,2.04);
  CHECK(sprite(id)==surface && pp_runtime_renderer_metrics(r).transform_draws==warm.transform_draws && pp_runtime_renderer_metrics(r).source_uploads==warm.source_uploads);
  pp_runtime_event(r,&(PPPlatformEvent){.surface=surface,.instance_id=id,.kind=PP_EVENT_WHEEL,.wheel_delta=.5,.modifiers=PP_MOD_ALT,.timestamp=3});
  pp_runtime_tick(r,3.02);pp_runtime_tick(r,3.2);
  CHECK(pp_runtime_renderer_metrics(r).transform_draws>warm.transform_draws && pp_runtime_renderer_metrics(r).source_uploads==warm.source_uploads && pp_runtime_renderer_metrics(r).readbacks==warm.readbacks);
  CHECK(pp_assets_source_metrics(pp_app_assets(app)).raster_builds==sources.raster_builds && pp_runtime_gpu_resources(r).retained_bytes==12);
  CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_NEW},&error));
  CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_RESOLVE_TRANSITION,.choice=PP_UNSAVED_DISCARD},&error));
  pointer=(PPVec2){500,500};pp_runtime_tick(r,4);
  CHECK(!r->failed && pp_scene_count(pp_app_scene(app))==0 && pp_runtime_gpu_resources(r).retained_bytes==0);
  pp_runtime_destroy(r);CHECK(!surfaces);pp_app_destroy(app);
  puts("PASS PNG runtime source/texture reuse and project-generation GPU cleanup");
}
static void test_svg_runtime_generation_cleanup(void) {
  PPApp *app=NULL;PPRuntime *r=NULL;PPError error={0};
  CHECK(pp_app_create(&app,&(PPAppOptions){.preferences_directory=""},&error));
  const char *svg="<svg width='13' height='7'><rect x='2' y='1' width='9' height='5' fill='red'/></svg>";
  PPItem item={.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=1,.id=pp_project_target(pp_app_project(app),PP_ANCHOR_TOP_LEFT)->id};
  CHECK(pp_asset_import(pp_app_assets(app),(PPBytes){(const uint8_t *)svg,strlen(svg)},PP_ASSET_SVG,&item.image_asset,&error));
  CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_EDIT_ITEM,.item_id=item.id,.item=&item},&error));
  CHECK(pp_runtime_create(&r,app,&error));uint64_t id=pull(r,1);
  for(int i=0;i<2000&&pp_runtime_presentation_metrics(r).worker_pending_jobs;i++){SDL_Delay(1);pp_runtime_tick(r,1.3);}
  pp_runtime_tick(r,1.3);CHECK(sprite(id)&&sprite(id)->window_id&&sprite(id)->source_width==96);
  PPPresentationMetrics before=pp_runtime_presentation_metrics(r);PPRendererMetrics renderer=pp_runtime_renderer_metrics(r);
  uint64_t owner_builds=pp_assets_source_metrics(pp_app_assets(app)).raster_builds;
  CHECK(before.worker_source_builds==1&&!before.worker_pending_jobs&&pp_runtime_gpu_resources(r).retained_bytes==19968);
  step(r,sprite(id),PP_EVENT_BUTTON_DOWN,320,210,2);step(r,sprite(id),PP_EVENT_POINTER_MOVE,400,300,2.01);step(r,sprite(id),PP_EVENT_BUTTON_UP,400,300,2.02);pp_runtime_tick(r,2.03);
  CHECK(pp_runtime_renderer_metrics(r).transform_draws==renderer.transform_draws&&pp_runtime_renderer_metrics(r).source_uploads==renderer.source_uploads);
  /* Twenty distinct physical targets exercise coalescing; subpixel-equivalent
   * endpoints now correctly share their requested raster. */
  for(int i=0;i<20;i++)pp_runtime_event(r,&(PPPlatformEvent){.surface=sprite(id),.instance_id=id,.kind=PP_EVENT_WHEEL,.wheel_delta=.125,.modifiers=PP_MOD_ALT,.timestamp=3+i*.001});
  CHECK(pp_runtime_presentation_metrics(r).worker_requests==before.worker_requests+20&&pp_runtime_presentation_metrics(r).worker_pending_jobs<=2);
  pp_runtime_tick(r,3.04);CHECK(pp_assets_source_metrics(pp_app_assets(app)).raster_builds==owner_builds);
  PPRasterWorker *service=r->worker;
  CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_NEW},&error));
  CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_RESOLVE_TRANSITION,.choice=PP_UNSAVED_DISCARD},&error));
  pointer=(PPVec2){500,500};pp_runtime_tick(r,4);
  CHECK(!r->failed&&!capture_owner&&!pp_scene_count(pp_app_scene(app))&&!pp_runtime_gpu_resources(r).retained_bytes&&r->worker==service);
  CHECK(!pp_raster_worker_status(service).reserved_bytes);
  for(int i=0;i<2000&&pp_raster_worker_status(service).pending_jobs;i++){SDL_Delay(1);pp_runtime_tick(r,4);}
  pp_runtime_tick(r,4);CHECK(!pp_raster_worker_status(service).pending_jobs&&!pp_raster_worker_status(service).reserved_bytes);
  pp_runtime_destroy(r);CHECK(!surfaces&&!capture_owner);pp_app_destroy(app);
  puts("PASS SVG runtime translation reuse, immediate bounded targets and project-generation cancellation keep one service and release owner CPU/GPU/input");
}
static void test_app_source_owner_survives_runtime_and_project_epochs(void) {
  PPApp *app=NULL;PPRuntime *runtime=NULL;PPError error={0};
  CHECK(pp_app_create(&app,&(PPAppOptions){.preferences_directory=""},&error));
  PPContentSources *sources=pp_app_content_sources(app,&error);CHECK(sources);
  PPContentConsumer *preview=NULL;CHECK(pp_content_consumer_create(sources,&preview,&error));
  const PPItem *item=pp_project_target(pp_app_project(app),PP_ANCHOR_TOP_LEFT);
  PPItemSnapshot *snapshot=pp_project_snapshot(pp_app_project(app),item->id);uint64_t revision=pp_item_snapshot_content_id(snapshot);
  PPContentFrame source={0};double next;
  CHECK(pp_content_frame(preview,snapshot,48,1,0,&source,&next,&error));CHECK(source.content_id==revision&&source.frame);
  CHECK(pp_runtime_create(&runtime,app,&error));pointer=(PPVec2){500,500};pp_runtime_tick(runtime,1);CHECK(!runtime->failed);
  CHECK(pp_app_command(app,&(PPCommand){.kind=PP_COMMAND_NEW},&error));pp_runtime_tick(runtime,2);CHECK(!runtime->failed);
  CHECK(pp_app_content_sources(app,&error)==sources);pp_runtime_destroy(runtime);runtime=NULL;
  CHECK(pp_app_content_sources(app,&error)==sources);
  for(int i=0;i<2000&&pp_raster_worker_status(pp_content_sources_worker(sources)).pending_jobs;i++){SDL_Delay(1);pp_content_sources_poll(sources);}
  pp_content_sources_poll(sources);CHECK(pp_content_frame(preview,snapshot,48,1,0,&source,&next,&error));
  CHECK(source.content_id==revision&&source.frame&&source.layout.x==pp_item_snapshot_size(snapshot).x);
  pp_content_accept(preview);pp_content_consumer_destroy(preview);pp_item_snapshot_release(snapshot);pp_app_destroy(app);CHECK(!surfaces);
  puts("PASS standalone preview source owner survives runtime closure and project replacement until app shutdown");
}
static void test_rejected_source_is_repreparable_by_new_consumer(void) {
  PPAssets *assets=NULL;PPContentSources *sources=NULL;PPItemSnapshot *snapshot=NULL;PPError error={0};
  CHECK(pp_assets_create(&assets,&error));CHECK(pp_content_sources_create(&sources,assets,&error));
  const char *svg="<svg width='13.5' height='7.5'><rect width='13.5' height='7.5' fill='red'/></svg>";
  PPItem item={.kind=PP_ITEM_IMAGE,.image_longest_dip=96,.opacity=1};
  CHECK(pp_asset_import(assets,(PPBytes){(const uint8_t *)svg,strlen(svg)},PP_ASSET_SVG,&item.image_asset,&error));
  CHECK(pp_item_snapshot_create(assets,&item,&snapshot,&error));
  PPContentConsumer *a=NULL,*b=NULL;CHECK(pp_content_consumer_create(sources,&a,&error));
  CHECK(pp_content_request(a,snapshot,96,1.5,&error));PPRasterWorker *worker=pp_content_sources_worker(sources);
  for(int i=0;i<2000&&pp_raster_worker_status(worker).pending_jobs;i++)SDL_Delay(1);
  pp_content_sources_poll(sources);const PPTextResult *candidate=pp_content_candidate(a,NULL);
  CHECK(candidate&&candidate->frame.width==144&&candidate->frame.height==80);
  CHECK(pp_content_gpu_reserve(sources,PP_RETAINED_GPU_LIMIT,&error));pp_content_reject(a);
  CHECK(!pp_content_candidate(a,NULL));
  for(int i=0;i<3;i++)CHECK(pp_content_request(a,snapshot,96,1.5,&error));
  CHECK(pp_raster_worker_status(worker).requests==1&&!pp_content_candidate(a,NULL));
  pp_content_gpu_release(sources,PP_RETAINED_GPU_LIMIT);
  CHECK(pp_content_consumer_create(sources,&b,&error));CHECK(pp_content_request(b,snapshot,96,1.5,&error));
  for(int i=0;i<2000&&pp_raster_worker_status(worker).pending_jobs;i++)SDL_Delay(1);
  pp_content_sources_poll(sources);candidate=pp_content_candidate(b,NULL);
  CHECK(candidate&&candidate->frame.width==144&&candidate->frame.height==80);
  CHECK(pp_raster_worker_status(worker).requests==2&&pp_raster_worker_status(worker).source_builds==2);
  CHECK(!pp_content_candidate(a,NULL));
  pp_content_accept(b);pp_content_consumer_destroy(a);pp_content_consumer_destroy(b);
  pp_content_sources_destroy(sources);pp_item_snapshot_release(snapshot);CHECK(!pp_assets_external_raster_bytes(assets));pp_assets_destroy(assets);
  puts("PASS a new consumer prepares an equal rejected-empty endpoint while the prior owner suppresses repeated attempts");
}
static PPVec2 rotated_center(PPVec2 center, PPVec2 pivot, double degrees) {
  double radians = degrees * 3.14159265358979323846 / 180;
  double x = center.x - pivot.x, y = center.y - pivot.y;
  return (PPVec2){pivot.x + x * cos(radians) - y * sin(radians),
                  pivot.y + x * sin(radians) + y * cos(radians)};
}
static void check_rotation_center(PPRuntime *r, uint64_t id, PPVec2 expected) {
  PPVec2 actual = pp_scene_get(pp_app_scene(r->app), id)->pose.center;
  CHECK(fabs(actual.x - expected.x) < .000001);
  CHECK(fabs(actual.y - expected.y) < .000001);
}
static void test_rotation_centers(void) {
  PPApp *app = NULL;
  PPRuntime *r = NULL;
  PPError error = {0};
  CHECK(pp_app_create(&app, &(PPAppOptions){.preferences_directory = ""}, &error));
  PPProject *arrows = test_arrow_project(pp_app_assets(app));
  PPItem item = *pp_project_target(arrows, PP_ANCHOR_TOP_LEFT);
  item.id = pp_project_target(pp_app_project(app), PP_ANCHOR_TOP_LEFT)->id;
  item.rotation_center = PP_ROTATION_MOUSE_POSITION;
  CHECK(pp_app_command(app, &(PPCommand){.kind = PP_COMMAND_EDIT_ITEM,
      .item_id = item.id, .item = &item}, &error));
  pp_project_destroy(arrows);
  CHECK(pp_runtime_create(&r, app, &error));
  uint64_t id = pull(r, 1);
  PPPose initial = pp_scene_get(pp_app_scene(app), id)->pose;
  PPVec2 pivot = {initial.center.x + 15, initial.center.y - 8};
  PPPlatformEvent wheel = {.surface = sprite(id), .instance_id = id,
      .kind = PP_EVENT_WHEEL, .wheel_delta = 9, .desktop_point = pivot,
      .timestamp = 2};
  pp_runtime_event(r, &wheel);
  pp_runtime_tick(r, 2.06);
  PPPose halfway = pp_scene_get(pp_app_scene(app), id)->pose;
  CHECK(halfway.angle_deg > initial.angle_deg && halfway.angle_deg < initial.angle_deg + 90);
  check_rotation_center(r, id, rotated_center(initial.center, pivot,
                                            halfway.angle_deg - initial.angle_deg));
  /* Wheel bursts accumulate angle while preserving the current eased pose. */
  wheel.timestamp = 2.06;
  wheel.wheel_delta = 9;
  pp_runtime_event(r, &wheel);
  pp_runtime_tick(r, 2.3);
  CHECK(pp_scene_get(pp_app_scene(app), id)->pose.angle_deg == initial.angle_deg + 180);
  check_rotation_center(r, id, rotated_center(initial.center, pivot, 180));
  /* A new mouse position becomes the pivot without jumping at retarget time. */
  PPPose before = pp_scene_get(pp_app_scene(app), id)->pose;
  wheel.desktop_point = (PPVec2){before.center.x - 9, before.center.y + 6};
  wheel.timestamp = 3; wheel.wheel_delta = -9;
  pp_runtime_event(r, &wheel);
  pp_runtime_tick(r, 3);
  check_rotation_center(r, id, before.center);
  pp_runtime_tick(r, 3.2);
  check_rotation_center(r, id, rotated_center(before.center, wheel.desktop_point, -90));
  /* Editing an item affects future pulls; the old sprite keeps its choice. */
  item = *pp_project_target(pp_app_project(app), PP_ANCHOR_TOP_LEFT);
  item.rotation_center = PP_ROTATION_SPRITE_CENTER;
  CHECK(pp_app_command(app, &(PPCommand){.kind = PP_COMMAND_EDIT_ITEM,
      .item_id = item.id, .item = &item}, &error));
  CHECK(pp_item_snapshot_item(pp_scene_get(pp_app_scene(app), id)->snapshot)->rotation_center ==
        PP_ROTATION_MOUSE_POSITION);
  uint64_t centered = pull(r, 4);
  PPVec2 center = pp_scene_get(pp_app_scene(app), centered)->pose.center;
  wheel.surface = sprite(centered); wheel.instance_id = centered;
  wheel.timestamp = 5; wheel.wheel_delta = 9;
  wheel.desktop_point = (PPVec2){center.x + 20, center.y + 10};
  pp_runtime_event(r, &wheel); pp_runtime_tick(r, 5.06);
  check_rotation_center(r, centered, center);
  pp_runtime_tick(r, 5.2); check_rotation_center(r, centered, center);
  /* Replacement adopts the replacement item's rotation choice. */
  item.rotation_center = PP_ROTATION_MOUSE_POSITION;
  CHECK(pp_scene_replace(pp_app_scene(app), centered, &item, 6, &error));
  pp_runtime_tick(r, 6);
  wheel.timestamp = 7;
  pp_runtime_event(r, &wheel); pp_runtime_tick(r, 7.2);
  check_rotation_center(r, centered, rotated_center(center, wheel.desktop_point, 90));
  /* A held press keeps its grabbed point after a mouse-centered rotation. */
  before = pp_scene_get(pp_app_scene(app), centered)->pose;
  pivot = (PPVec2){before.center.x + 7, before.center.y + 3};
  step(r, sprite(centered), PP_EVENT_BUTTON_DOWN, pivot.x, pivot.y, 8);
  wheel.desktop_point = pivot; wheel.timestamp = 8.01;
  pp_runtime_event(r, &wheel); pp_runtime_tick(r, 8.2);
  PPVec2 rotated = rotated_center(before.center, pivot, 90);
  check_rotation_center(r, centered, rotated);
  step(r, sprite(centered), PP_EVENT_POINTER_MOVE, pivot.x + 20, pivot.y + 10, 8.21);
  pp_runtime_tick(r, 8.21);
  check_rotation_center(r, centered, (PPVec2){rotated.x + 20, rotated.y + 10});
  /* Rotation and Alt+wheel resizing share the held mouse point during drag. */
  before = pp_scene_get(pp_app_scene(app), centered)->pose;
  pivot = (PPVec2){pivot.x + 20, pivot.y + 10};
  wheel.desktop_point = pivot; wheel.timestamp = 8.22;
  pp_runtime_event(r, &wheel);
  wheel.modifiers = PP_MOD_ALT; wheel.wheel_delta = 1;
  pp_runtime_event(r, &wheel);
  const double times[] = {8.28, 8.5};
  for (size_t tick = 0; tick < 2; tick++) {
    pp_runtime_tick(r, times[tick]);
    PPPose current = pp_scene_get(pp_app_scene(app), centered)->pose;
    PPVec2 expected = rotated_center(before.center, pivot, current.angle_deg - before.angle_deg);
    double ratio = current.longest_dip / before.longest_dip;
    expected = (PPVec2){pivot.x + (expected.x - pivot.x) * ratio,
                        pivot.y + (expected.y - pivot.y) * ratio};
    check_rotation_center(r, centered, expected);
  }
  wheel.modifiers = 0; wheel.wheel_delta = 9;
  /* Cross to the 200% display while rotation is still easing. The same local
   * artwork point must stay at the new cursor as the remaining arc plays. */
  before = pp_scene_get(pp_app_scene(app), centered)->pose;
  wheel.timestamp = 8.52;
  pp_runtime_event(r, &wheel); pp_runtime_tick(r, 8.56);
  step(r, sprite(centered), PP_EVENT_POINTER_MOVE, 2400, 500, 8.57);
  step(r, sprite(centered), PP_EVENT_POINTER_MOVE, 2420, 510, 8.571);
  pp_runtime_tick(r, 8.58);
  PPPose cross = pp_scene_get(pp_app_scene(app), centered)->pose;
  PPVec2 offset = rotated_center(before.center, pivot, cross.angle_deg - before.angle_deg);
  check_rotation_center(r, centered, (PPVec2){2420 + (offset.x - pivot.x) * 2,
                                            510 + (offset.y - pivot.y) * 2});
  /* Two further crossings can arrive before any frame updates s->scale. */
  step(r, sprite(centered), PP_EVENT_POINTER_MOVE, 800, 500, 8.59);
  step(r, sprite(centered), PP_EVENT_POINTER_MOVE, 2400, 500, 8.591);
  pp_runtime_tick(r, 8.6);
  cross = pp_scene_get(pp_app_scene(app), centered)->pose;
  offset = rotated_center(before.center, pivot, cross.angle_deg - before.angle_deg);
  check_rotation_center(r, centered, (PPVec2){2400 + (offset.x - pivot.x) * 2,
                                            500 + (offset.y - pivot.y) * 2});
  step(r, sprite(centered), PP_EVENT_BUTTON_UP, 2400, 500, 8.61);
  pp_runtime_tick(r, 8.7);
  offset = rotated_center(before.center, pivot, 90);
  check_rotation_center(r, centered, (PPVec2){2400 + (offset.x - pivot.x) * 2,
                                            500 + (offset.y - pivot.y) * 2});
  /* Verify desktop coordinates with zero easing on the second display. */
  PPProjectSettings gestures = *pp_project_settings(pp_app_project(app));
  gestures.easing_seconds = 0;
  CHECK(pp_app_command(app, &(PPCommand){.kind = PP_COMMAND_SET_INTERACTION,
      .interaction = gestures}, &error));
  pp_runtime_tick(r, 9);
  before = pp_scene_get(pp_app_scene(app), centered)->pose;
  wheel.desktop_point = (PPVec2){before.center.x + 30, before.center.y - 16};
  wheel.timestamp = 10;
  pp_runtime_event(r, &wheel); pp_runtime_tick(r, 10);
  check_rotation_center(r, centered, rotated_center(before.center, wheel.desktop_point, 90));
  before = pp_scene_get(pp_app_scene(app), centered)->pose;
  wheel.modifiers = PP_MOD_ALT; wheel.wheel_delta = 1; wheel.timestamp = 11;
  pp_runtime_event(r, &wheel); pp_runtime_tick(r, 11);
  check_rotation_center(r, centered, before.center);
  CHECK(pp_scene_get(pp_app_scene(app), centered)->pose.longest_dip > before.longest_dip);
  pp_runtime_destroy(r); pp_app_destroy(app);
  CHECK(!surfaces);
  puts("PASS mouse/sprite rotation centers, eased arcs, wheel bursts, new pivots, snapshot isolation and replacement.");
}
int main(int argc,char **argv) {
  if(argc==2&&!strcmp(argv[1],"--rotation-centers")){test_rotation_centers();return 0;}
  test_rotation_centers();
  if(argc==2&&!strcmp(argv[1],"--failure-isolation")){test_failure_isolation();return 0;}
  test_rejected_source_is_repreparable_by_new_consumer();
  test_app_source_owner_survives_runtime_and_project_epochs();
#if defined(_WIN32) && !defined(PP_TEST_COMPAT_PRESENTATION)
  if(argc==2&&!strcmp(argv[1],"--svg")){test_svg_runtime_generation_cleanup();return 0;}
  test_svg_runtime_generation_cleanup();
  test_png_runtime_reuse_and_generation_cleanup();
#endif
  test_runtime_ownership();
  test_failure_isolation();
  test_external_change_cancels_owned_input();
  puts("PASS runtime ownership, pulls, wheel, replacement, empty drawer, "
       "removal, stale IDs, transitions and failure isolation");
  return 0;
}
/* These boundary mocks have no GL output storage. Native desktop tests cover
 * retained color allocation and publication. */
size_t pp_renderer_output_candidate_bytes(const PPRenderer *r,int w,int h){(void)r;(void)w;(void)h;return 0;}
size_t pp_renderer_output_bytes(const PPRenderer *r){(void)r;return 0;}
bool pp_renderer_prepare_source_output(PPRenderer *r,const PPFrame *frame,PPSharedTexture *shared,
    uint64_t id,uint64_t revision,int w,int h,PPError *error) {
  (void)w;(void)h;
  if(shared)return pp_renderer_bind_shared(r,shared,id,revision,error);
  if(frame)return pp_renderer_upload_source(r,frame,id,revision,error);
  return true;
}
void pp_renderer_release_output(PPRenderer *r){(void)r;}
bool pp_renderer_refresh(PPRenderer *r,PPError *error){(void)error;r->metrics.presentation_refreshes++;return true;}
bool pp_renderer_destroy_retained(PPRenderer *r){pp_renderer_destroy(r);return true;}
