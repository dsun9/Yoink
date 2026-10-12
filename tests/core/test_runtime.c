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
PPRendererMetrics pp_renderer_metrics(const PPRenderer *r) {return r==(PPRenderer *)1?(PPRendererMetrics){cpu_transforms,cpu_transforms,cpu_transforms*2}:r?r->metrics:(PPRendererMetrics){0};}
bool pp_surface_create_gl(PPSurface **out,PPError *error) {if(!pp_surface_create(out,error))return false;(*out)->window_id=++direct_window_serial;return true;}
uint32_t pp_surface_window_id(const PPSurface *s) {return s?s->window_id:0;}
bool pp_surface_place_gl(PPSurface *s,PPRect b,bool interactive,PPError *error) {(void)error;s->bounds=b;s->shown=interactive;if(!interactive && capture_owner==s)capture_owner=NULL;return true;}
void pp_surface_set_rectangular(PPSurface *s,bool enabled) {(void)s;(void)enabled;}
void pp_surface_inherit_order(PPSurface *s,PPSurface *old) {(void)s;(void)old;}
bool pp_renderer_create_for_window(PPRenderer **out,uint32_t id,PPError *error) {
    (void)error;*out=calloc(1,sizeof(**out));CHECK(*out);
    for(PPSurface *s=surfaces;s;s=s->next)if(s->window_id==id)(*out)->surface=s;
    CHECK((*out)->surface);return true;
}
bool pp_renderer_upload_source(PPRenderer *r,const PPFrame *frame,uint64_t id,uint64_t revision,PPError *error) {
    (void)error;if(r->content_id==id&&r->revision==revision)return true;
    r->surface->source_width=frame->width;r->surface->red=frame->pixels[0];r->surface->blue=frame->pixels[2];
    r->content_id=id;r->revision=revision;r->metrics.source_uploads++;return true;
}
bool pp_renderer_draw(PPRenderer *r,PPVec2 layout,double size,double angle,double opacity,double density,PPError *error) {
    (void)layout;(void)size;(void)angle;(void)opacity;(void)density;
    if(r->surface==fail_surface){if(error)*error=(PPError){PP_ERROR_RENDER,"Injected direct presentation failure"};return false;}
    r->metrics.transform_draws++;return true;
}
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
  /* Distinct native failure after candidate allocation: remove only the
   * failed sprite and release both candidate and previous DIB charges. */
  size_t charged = pp_assets_external_raster_bytes(pp_app_assets(app));
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
  CHECK(pp_assets_external_raster_bytes(pp_app_assets(app)) < charged);
  CHECK(strstr(pp_app_diagnostic(app), "Injected presentation failure"));
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
  pp_runtime_destroy(r);
  CHECK(!surfaces);
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
int main(void) {
#if defined(_WIN32)
  test_png_runtime_reuse_and_generation_cleanup();
#endif
  test_runtime_ownership();
  test_failure_isolation();
  test_external_change_cancels_owned_input();
  puts("PASS runtime ownership, pulls, wheel, replacement, empty drawer, "
       "removal, stale IDs, transitions and failure isolation");
  return 0;
}
