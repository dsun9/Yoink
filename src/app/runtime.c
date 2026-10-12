#include "../ui/drawer_internal.h"
#include "presenter_internal.h"
#include "runtime_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

PPSchedule pp_runtime_schedule(const PPRuntime *r, double now) {
  PPSchedule s={now+1.0/60,HUGE_VAL,HUGE_VAL,HUGE_VAL,HUGE_VAL,HUGE_VAL};
  if (!r || r->failed || !r->presenter) return s;
  PPAnchorPresenter *p=r->presenter;
  for (size_t i=0;i<p->instance_count;i++) {
    const PPSprite *v=p->instances+i;
    s.gif=fmin(s.gif,v->next_frame);
    if(v->rotating) s.easing=fmin(s.easing,pp_schedule_tick(now,v->rotate_start+p->gestures.easing_seconds));
    if(v->resizing) s.easing=fmin(s.easing,pp_schedule_tick(now,v->resize_start+p->gestures.easing_seconds));
  }
  for (size_t i=0;i<p->anchor_count;i++) {
    if(p->anchors[i].shown) s.gif=fmin(s.gif,p->anchors[i].next_frame);
    const PPAnchorReveal *reveal=p->runtime.entries+i;
    if(reveal->leaving) s.fade=fmin(s.fade,pp_schedule_tick(now,reveal->leave_time+p->gestures.easing_seconds));
  }
  if(r->drawer) s.ui=now+1.0/60;
  return s;
}
PPRendererMetrics pp_runtime_renderer_metrics(const PPRuntime *r) {
  return r?pp_anchor_presenter_renderer_metrics(r->presenter):(PPRendererMetrics){0};
}
PPGpuResources pp_runtime_gpu_resources(const PPRuntime *r) {
  return pp_anchor_presenter_gpu_resources(r?r->presenter:NULL);
}
PPPresentationMetrics pp_runtime_presentation_metrics(const PPRuntime *r) {
  return pp_anchor_presenter_metrics(r ? r->presenter : NULL);
}

static void failure(PPRuntime *r) {
  r->failed = true;
  pp_app_report(r->app, &r->error);
  pp_drawer_close(r->drawer);
  r->drawer = NULL;
  pp_drawer_cache_destroy(&r->drawer_cache);
  pp_anchor_presenter_destroy(r->presenter);
  r->presenter = NULL;
  pp_platform_release_capture();
  fprintf(stderr, "Sprite runtime closed: %s\n", r->error.message);
}
static bool synchronize(PPRuntime *r) {
  if (!r || r->failed)
    return false;
  uint64_t generation = pp_app_generation(r->app);
  if (generation != r->generation) {
    /* The old project has already been freed. Destroy only owned native
     * resources; presenter destruction does not read its project borrow. */
    pp_drawer_close(r->drawer);
    r->drawer = NULL;
    pp_anchor_presenter_destroy(r->presenter);
    r->presenter = NULL;
    r->generation = generation;
  }
  if (r->presenter)
    pp_sprites_sync(r->presenter);
  if (r->drawer && !pp_drawer_valid(r->drawer)) {
    pp_drawer_close(r->drawer);
    r->drawer = NULL;
  }
  return true;
}
bool pp_runtime_create(PPRuntime **out, PPApp *app, PPError *error) {
  if (out)
    *out = NULL;
  if (!out || !app) {
    if (error)
      *error = (PPError){PP_ERROR_ARGUMENT, "Missing runtime output or app"};
    return false;
  }
  PPRuntime *r = calloc(1, sizeof(*r));
  if (!r) {
    if (error)
      *error = (PPError){PP_ERROR_MEMORY, "Cannot allocate runtime"};
    return false;
  }
  r->app = app;
  r->generation = pp_app_generation(app);
  if(!pp_raster_worker_create(&r->worker,error)) {free(r);return false;}
  if (!pp_renderer_create(&r->renderer, error)) {
    pp_raster_worker_destroy(r->worker);
    free(r);
    return false;
  }
  r->drawer_cache.renderer=r->renderer;
  *out = r;
  return true;
}
void pp_runtime_prepare_ui(PPRuntime *r) {
  PPDisplay *displays=NULL;size_t count=0;PPError error={0};PPVec2 pointer={0};
  if (pp_platform_displays(&displays,&count,&error) && count) {
    pp_platform_pointer(&pointer);
    const PPDisplay *display=pp_display_at(displays,count,pointer);
    if (!display) display=displays;
    if (!pp_drawer_cache_warm(&r->drawer_cache,display,&error)) pp_app_report(r->app,&error);
  }
  pp_displays_free(displays);
}
void pp_runtime_event(PPRuntime *r, const PPPlatformEvent *event) {
  if (!event || !synchronize(r))
    return;
  if (r->drawer) {
    pp_drawer_event(r->drawer, event);
    if (r->drawer->dismissed) {
      pp_drawer_close(r->drawer);
      r->drawer = NULL;
    }
  }
  if (!r->presenter || event->window_id)
    return;
  if (!pp_anchor_presenter_event(r->presenter, event, &r->error)) {
    failure(r);
    return;
  }
  uint64_t id = r->presenter->drawer_request;
  r->presenter->drawer_request = 0;
  if (id && pp_scene_get(pp_app_scene(r->app), id)) {
    pp_drawer_close(r->drawer);
    r->drawer = NULL;
    if (!pp_drawer_open(&r->drawer, r->app, id, &r->error) ||
        !pp_drawer_use_cache(r->drawer,&r->drawer_cache,&r->error)) {
      pp_app_report(r->app, &r->error);
      pp_drawer_close(r->drawer);r->drawer=NULL;
    }
  }
}
void pp_runtime_tick(PPRuntime *r, double now) {
  if (!synchronize(r))
    return;
  if (!r->presenter && !pp_anchor_presenter_create_shared(
                           &r->presenter, r->renderer, pp_app_project(r->app),
                           pp_app_scene(r->app), &r->error)) {
    failure(r);
    return;
  }
  r->presenter->worker=r->worker;r->presenter->borrowed_worker=true;
  PPDisplay *displays = NULL;
  size_t count = 0;
  PPVec2 pointer = {0};
  if (!pp_platform_displays(&displays, &count, &r->error)) {
    failure(r);
    return;
  }
  pp_platform_pointer(&pointer);
  bool ok = pp_anchor_presenter_update(r->presenter, displays, count, pointer,
                                       now, &r->error);
  if (ok && r->drawer)
    pp_drawer_layout(r->drawer, displays, count);
  pp_displays_free(displays);
  if (!ok)
    failure(r);
  else if (r->presenter->notice.code) {
    pp_app_report(r->app, &r->presenter->notice);
    r->presenter->notice = (PPError){0};
  }
}
void pp_runtime_draw_ui(PPRuntime *r, double now) {
  if (!synchronize(r) || !r->drawer)
    return;
  if (!pp_drawer_draw(r->drawer, now, &r->error)) {
    pp_app_report(r->app, &r->error);
    r->drawer->dismissed = true;
  }
  if (r->drawer->dismissed) {
    pp_drawer_close(r->drawer);
    r->drawer = NULL;
  }
}
void pp_runtime_destroy(PPRuntime *r) {
  if (!r)
    return;
  pp_drawer_close(r->drawer);
  pp_drawer_cache_destroy(&r->drawer_cache);
  pp_anchor_presenter_destroy(r->presenter);
  pp_raster_worker_destroy(r->worker);
  pp_renderer_destroy(r->renderer);
  free(r);
}
