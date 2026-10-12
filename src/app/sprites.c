#include "../core/geometry_internal.h"
#include "presenter_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const PPDisplay *display_by_id(const PPDisplay *d, size_t count,
                                      uint64_t id) {
  for (size_t i = 0; i < count; i++)
    if (d[i].id == id)
      return d + i;
  return NULL;
}
static bool present_source(PPAnchorPresenter *p,PPSurface *surface,const PPFrame *source,
                PPVec2 layout,PPRect viewport,PPPose pose,const PPDisplay *display,size_t *retained,PPError *error) {
  double scale = display->drawable_pixels_per_dip;
  double desktop_per_pixel = display->desktop_units_per_dip / scale;
  int width, height;
  bool extent=pp_geometry_source_extent(layout,viewport,pose.longest_dip,pose.angle_deg,scale,&width,&height);
  if (!extent ||
      width > 8192 || height > 8192) {
    if (error) {
      error->code = PP_ERROR_RESOURCE_LIMIT;
      snprintf(error->message, sizeof(error->message),
               "Presented raster exceeds 8192 pixels per axis");
    }
    return false;
  }
  size_t frame_bytes = (size_t)width * height * 4;
  if (!pp_assets_reserve_raster(p->assets, frame_bytes, error))
    return false;
  PPFrame frame = {0};
  bool ok=pp_renderer_transform_color_source(p->renderer,source,layout,viewport,pose.longest_dip,pose.angle_deg,pose.opacity,scale,&frame,error);
  /* Old + candidate copies overlap until native presentation commits. */
  size_t native_bytes = pp_surface_raster_bytes(width, height);
  bool reserved =
      ok && pp_assets_reserve_raster(p->assets, native_bytes, error);
  if (reserved) {
    ok = pp_surface_present(surface, &frame,
                            (PPRect){round(pose.center.x - frame.width * desktop_per_pixel * .5),
                                     round(pose.center.y - frame.height * desktop_per_pixel * .5),
                                     frame.width * desktop_per_pixel, frame.height * desktop_per_pixel},
                            error);
    if (ok) {
      p->metrics.native_publications++;
      pp_assets_release_raster(p->assets, *retained);
      *retained = native_bytes;
    } else {
      p->failed_surface = surface;
      pp_assets_release_raster(p->assets, native_bytes);
    }
  } else
    ok = false;
  pp_frame_destroy(&frame);
  pp_assets_release_raster(p->assets, frame_bytes);
  return ok;
}
bool pp_present(PPAnchorPresenter *p,PPSurface *surface,const PPFrame *source,
    PPPose pose,const PPDisplay *display,size_t *retained,PPError *error) {
  PPVec2 layout={source->width,source->height};
  pp_surface_set_rectangular(surface,true);
  return present_source(p,surface,source,layout,(PPRect){0,0,layout.x,layout.y},pose,display,retained,error);
}
bool pp_present_source(PPAnchorPresenter *p,PPSurface *surface,PPSourcePresentation source,
    PPPose pose,const PPDisplay *display,size_t *retained,PPError *error) {
  pp_surface_set_rectangular(surface,true);
  if(!source.frame||!source.visual_alpha||pose.opacity<=0){pp_surface_hide(surface);return true;}
  PPRect rect=source.source_rect;if(!rect.w&&!rect.h)rect=(PPRect){0,0,source.layout.x,source.layout.y};
  return present_source(p,surface,source.frame,source.layout,rect,pose,display,retained,error);
}
bool pp_capture_present_content(PPAnchorPresenter *p, PPSurface *surface,
                        PPItemSnapshot *snapshot, PPPose pose,
                        const PPDisplay *display, double elapsed, double *next,
                        size_t *bytes, PPError *error) {
  PPVec2 size = pp_item_snapshot_size(snapshot);
  double scale = display->drawable_pixels_per_dip * pose.longest_dip /
                 fmax(size.x, size.y);
  const PPFrame *source = NULL;
  if (!pp_item_snapshot_frame(snapshot, elapsed, scale, &source, next, error))
    return false;
  if(pp_snapshot_is_svg(snapshot)&&!pp_source_visual_alpha(source)) {
    pp_surface_hide(surface);return true;
  }
  PPVec2 layout=pp_snapshot_is_svg(snapshot)?size:(PPVec2){source->width,source->height};
  pp_surface_set_rectangular(surface,true);
  return present_source(p,surface,source,layout,(PPRect){0,0,layout.x,layout.y},pose,display,bytes,error);
}
PPSprite *pp_sprites_find(PPAnchorPresenter *p, uint64_t id) {
  for (size_t i = 0; i < p->instance_count; i++)
    if (p->instances[i].id == id)
      return p->instances + i;
  return NULL;
}
PPSprite *pp_sprites_create(PPAnchorPresenter *p, PPAnchor anchor, PPPose pose,
                            double now, PPError *error) {
  const PPItem *item=pp_project_target(p->project,anchor);
  PPItemSnapshot *snapshot=item?pp_project_snapshot(p->project,item->id):NULL;
  PPSprite *sprite=pp_sprites_create_snapshot(p,snapshot,anchor,pose,now,error);
  pp_item_snapshot_release(snapshot);return sprite;
}
PPSprite *pp_sprites_create_snapshot(PPAnchorPresenter *p,PPItemSnapshot *snapshot,
    PPAnchor anchor,PPPose pose,double now,PPError *error) {
  PPSprite *items =
      realloc(p->instances, (p->instance_count + 1) * sizeof(*items));
  if (!items) {
    if (error)
      *error = (PPError){PP_ERROR_MEMORY, "Cannot allocate sprite mapping"};
    return NULL;
  }
  p->instances = items;
  PPSprite *s = items + p->instance_count;
  memset(s, 0, sizeof(*s));
  s->pose = pose;
  s->dirty = true;
  if (!pp_scene_spawn_snapshot(p->scene, snapshot, anchor, pose.center, now, &s->id,
                      error))
    return NULL;
  bool direct=false;
#if !defined(PP_TEST_COMPAT_PRESENTATION) && (defined(_WIN32) || defined(PP_TEST_DIRECT_TEXT))
  direct=pp_snapshot_is_svg(pp_scene_get(p->scene,s->id)->snapshot)||pp_snapshot_is_png(pp_scene_get(p->scene,s->id)->snapshot)||pp_snapshot_is_gif(pp_scene_get(p->scene,s->id)->snapshot)||pp_item_snapshot_text_result(pp_scene_get(p->scene,s->id)->snapshot)!=NULL;
#endif
  if (!(direct?pp_surface_create_gl(&s->surface,error):pp_surface_create(&s->surface, error))) {
    pp_scene_delete(p->scene, s->id);
    return NULL;
  }
  pp_surface_set_instance_id(s->surface, s->id);
  s->seen_snapshot =
      pp_item_snapshot_retain(pp_scene_get(p->scene, s->id)->snapshot);
  s->seen_animation_start=pp_scene_get(p->scene,s->id)->animation_start;
  p->instance_count++;
  return s;
}
bool pp_sprites_update(PPAnchorPresenter *p, double now, PPError *error) {
  pp_scalable_poll(p);
  const PPDisplay *d = p->displays;
  size_t count = p->display_count;
  for (size_t i = 0; i < p->instance_count;) {
    PPSprite *s = p->instances + i;
    const PPDisplay *display = display_by_id(d, count, s->display_id);
    if (s->rotating) {
      s->pose.angle_deg =
          pp_ease_value(s->ease_from.angle_deg, s->ease_target.angle_deg,
                        now - s->rotate_start, p->gestures.easing_seconds);
      s->rotating = now - s->rotate_start < p->gestures.easing_seconds;
      s->dirty = true;
    }
    if (s->resizing) {
      double previous = s->pose.longest_dip;
      s->pose.longest_dip =
          pp_ease_value(s->ease_from.longest_dip, s->ease_target.longest_dip,
                        now - s->resize_start, p->gestures.easing_seconds);
      if (s->dragging && previous > 0) {
        s->grab_dip.x *= s->pose.longest_dip / previous;
        s->grab_dip.y *= s->pose.longest_dip / previous;
        if (display)
          s->pose.center =
              pp_display_drag_center(display, s->drag_pointer, s->grab_dip);
      }
      s->resizing = now - s->resize_start < p->gestures.easing_seconds;
      s->dirty = true;
    }
    if (!display) {
      display = pp_display_at(d, count, s->pose.center);
      if (display)
        s->display_id = display->id;
    }
    if (!display) {
      pp_sprite_hide(p,s);
      s->dirty = true;
      i++;
      continue;
    }
    double scale = display->desktop_units_per_dip;
    if (s->dirty || s->scale != scale || s->drawable_density!=display->drawable_pixels_per_dip || now >= s->next_frame ||
        (s->raster_retry&&pp_raster_worker_can_request(p->worker,p->generation,pp_content_target(s->content).instance_id))) {
      if (!pp_scene_set_pose(p->scene, s->id, s->pose, error))
        return false;
      const PPSceneInstance *instance = pp_scene_get(p->scene, s->id);
      double elapsed = fmax(0, now - instance->animation_start), next;
      if (!pp_sprite_present(p,s,instance,display,elapsed,&next,error)) {
        pp_scalable_cancel(p,s);
        bool native = p->failed_surface == s->surface;
        if (!native && error && error->code == PP_ERROR_RENDER && !pp_snapshot_is_png(instance->snapshot) &&
            !pp_snapshot_is_gif(instance->snapshot) && !pp_item_snapshot_text_result(instance->snapshot) && !s->retained.valid)
          return false;
        if (error)
          p->notice = *error;
        if (s->interaction.pressed || s->dragging)
          pp_surface_cancel_input(s->surface);
        if (!native && ((s->raster_bytes && s->seen_snapshot) || s->retained.valid)) {
          /* A reserve/size rejection leaves the old native pixels
           * intact. Restore the scene to exactly those pixels and
           * cancel this target; other sprites continue normally. */
          s->pose = s->drawn;
          s->rotating = s->resizing = s->dragging = false;
          s->sharp_pending = false;
          s->interaction = (PPInteraction){0};
          s->dirty = false;
          s->next_frame = HUGE_VAL;
          PPItemSnapshot *usable=s->retained.snapshot?s->retained.snapshot:s->seen_snapshot;
          double start=s->retained.snapshot?s->retained.animation_start:s->seen_animation_start;
          if(pp_snapshot_is_gif(usable)) {
            PPFrameSelection selected;
            if(pp_item_snapshot_select_frame(usable,fmax(0,now-start),1,&selected,NULL))
              s->next_frame=start+selected.next_change;
          }
          pp_scene_restore(p->scene,s->id,usable,s->pose,start,NULL);
          pp_item_snapshot_release(s->seen_snapshot);s->seen_snapshot=pp_item_snapshot_retain(usable);
          s->replaced=false;
          i++;
          continue;
        }
        p->failed_surface = NULL;
        pp_scene_delete(p->scene, s->id);
        pp_sprite_release_presentation(p,s);pp_surface_destroy(s->surface);
        pp_item_snapshot_release(s->seen_snapshot);
        memmove(s, s + 1, (p->instance_count - i - 1) * sizeof(*s));
        p->instance_count--;
        continue;
      }
      s->next_frame = instance->animation_start + next;
      if (s->sharp_pending && !pp_snapshot_is_scalable(instance->snapshot) && !s->resizing && s->pose.longest_dip == s->ease_target.longest_dip) {
        p->metrics.sharp_publications++;
        p->metrics.last_sharp_input_timestamp = s->resize_start;
        p->metrics.last_sharp_instance_id = s->id;
        p->metrics.last_sharp_content_revision = pp_item_snapshot_revision(instance->snapshot);
        p->metrics.last_sharp_longest_dip = s->pose.longest_dip;
        p->metrics.last_sharp_drawable_density = display->drawable_pixels_per_dip;
        s->sharp_pending = false;
      }
      s->dirty = false;
      s->replaced = false;
      if(s->seen_snapshot!=instance->snapshot) {pp_item_snapshot_release(s->seen_snapshot);s->seen_snapshot=pp_item_snapshot_retain(instance->snapshot);}
      s->seen_animation_start=instance->animation_start;
      s->drawn = s->pose;
      s->scale = scale;
      s->drawable_density=display->drawable_pixels_per_dip;
    }
    i++;
  }
  return true;
}
bool pp_sprites_event(PPAnchorPresenter *p, const PPPlatformEvent *event,
                      PPError *error) {
  PPSprite *s = pp_sprites_find(p, event->instance_id);
  if (!s || event->surface != s->surface)
    return true;
  s->interaction.displayed_pose = s->pose;
  if (event->kind == PP_EVENT_BUTTON_DOWN)
    pp_surface_raise(s->surface);
  if (event->kind == PP_EVENT_BUTTON_DOWN && event->button == PP_BUTTON_LEFT) {
    if (s->scale <= 0)
      return true;
    /* The press belongs to the presented raster. Its center can be across
     * a seam from the display that supplied that raster's scale. */
    s->grab_dip =
        (PPVec2){(event->desktop_point.x - s->pose.center.x) / s->scale,
                 (event->desktop_point.y - s->pose.center.y) / s->scale};
    pp_platform_capture(s->surface, NULL);
  }
  PPInteractionResult result;
  pp_interaction_step(&s->interaction, event, &p->gestures, &result);
  if (result.command == PP_COMMAND_BEGIN_DRAG ||
      result.command == PP_COMMAND_MOVE_DRAG ||
      result.command == PP_COMMAND_END_DRAG) {
    const PPDisplay *d =
        pp_display_at(p->displays, p->display_count, event->desktop_point);
    if (d) {
      s->pose.center =
          pp_display_drag_center(d, event->desktop_point, s->grab_dip);
      s->display_id = d->id;
      s->drag_pointer = event->desktop_point;
      s->dragging = result.command != PP_COMMAND_END_DRAG;
      s->dirty = true;
    }
  }
  if (event->kind == PP_EVENT_CANCEL) {
    s->dragging = false;
    s->sharp_pending = false;
  }
  if (result.command == PP_COMMAND_ROTATE) {
    s->ease_from.angle_deg = s->pose.angle_deg;
    s->ease_target.angle_deg = result.pose.angle_deg;
    s->rotate_start = event->timestamp;
    s->rotating = true;
  }
  if (result.command == PP_COMMAND_RESIZE) {
    s->ease_from.longest_dip = s->pose.longest_dip;
    s->ease_target.longest_dip = result.pose.longest_dip;
    s->resize_start = event->timestamp;
    s->resizing = true;
    s->sharp_pending = true;
    const PPDisplay *d=display_by_id(p->displays,p->display_count,s->display_id);
    const PPSceneInstance *instance=pp_scene_get(p->scene,s->id);
    if(d&&instance&&pp_surface_window_id(s->surface)&&!pp_scalable_request(p,s,instance,d,s->ease_target.longest_dip,error)) {
      if(error)p->notice=*error;pp_scalable_cancel(p,s);s->resizing=false;s->sharp_pending=false;
      s->interaction.target_pose.longest_dip=s->pose.longest_dip;s->ease_target.longest_dip=s->pose.longest_dip;
    }
  }
  if (result.command == PP_COMMAND_OPEN_DRAWER)
    p->drawer_request = s->id;
  if (event->kind == PP_EVENT_CANCEL ||
      (event->kind == PP_EVENT_BUTTON_UP && event->button == PP_BUTTON_LEFT))
    pp_surface_cancel_input(s->surface);
  if (s->dirty && !pp_scene_set_pose(p->scene, s->id, s->pose, error))
    return false;
  if (result.command == PP_COMMAND_DELETE) {
    size_t index = (size_t)(s - p->instances);
    pp_surface_cancel_input(s->surface);
    pp_sprite_release_presentation(p,s);pp_surface_destroy(s->surface);
    pp_item_snapshot_release(s->seen_snapshot);
    pp_scene_delete(p->scene, s->id);
    memmove(s, s + 1, (p->instance_count - index - 1) * sizeof(*s));
    p->instance_count--;
  }
  return true;
}

void pp_sprites_clear(PPAnchorPresenter *p) {
  for (size_t i = 0; i < p->instance_count; i++) {
    pp_sprite_release_presentation(p,p->instances+i);pp_surface_destroy(p->instances[i].surface);
    pp_item_snapshot_release(p->instances[i].seen_snapshot);
  }
  free(p->instances);
  p->instances = NULL;
  p->instance_count = 0;
}
void pp_sprites_sync(PPAnchorPresenter *p) {
  for (size_t i = 0; i < p->instance_count;) {
    PPSprite *s = p->instances + i;
    const PPSceneInstance *instance = pp_scene_get(p->scene, s->id);
    if (!instance) {
      pp_sprite_release_presentation(p,s);pp_surface_destroy(s->surface);
      pp_item_snapshot_release(s->seen_snapshot);
      memmove(s, s + 1, (p->instance_count - i - 1) * sizeof(*s));
      p->instance_count--;
      continue;
    }
    if (instance->snapshot != s->seen_snapshot ||
        memcmp(&instance->pose, &s->pose, sizeof(s->pose))) {
      /* End the matching native press before discarding the logical gesture.
       * Another sprite may own a newer press; its capture must survive. */
      pp_surface_cancel_input(s->surface);
      if (instance->snapshot != s->seen_snapshot) {
        pp_scalable_cancel(p,s);
        s->replaced = true;
      }
      else if(instance->pose.longest_dip!=s->pose.longest_dip)pp_scalable_cancel(p,s);
      s->pose = instance->pose;
      s->interaction = (PPInteraction){0};
      s->rotating = s->resizing = s->dragging = false;
      s->sharp_pending = false;
      s->dirty = true;
    }
    i++;
  }
}
