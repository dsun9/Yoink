#include "presenter_internal.h"
#include "../core/geometry_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static bool fail(PPError *error,int code,const char *message) {
  if(error) *error=(PPError){code};
  if(error) snprintf(error->message,sizeof(error->message),"%s",message);
  return false;
}
bool pp_snapshot_is_png(const PPItemSnapshot *snapshot) {
  const PPItem *item=pp_item_snapshot_item(snapshot);
  if(!item || item->kind!=PP_ITEM_IMAGE) return false;
  PPAsset *asset=pp_asset_retain(pp_item_snapshot_assets(snapshot),item->image_asset);
  bool png=asset && pp_asset_kind(asset)==PP_ASSET_PNG;pp_asset_release(asset);return png;
}
bool pp_source_visual_alpha(const PPFrame *source) {
  if(!source || !source->pixels) return false;
  for(int y=0;y<source->height;y++) for(int x=0;x<source->width;x++)
    if(source->pixels[(size_t)y*source->stride+(size_t)x*4+3]) return true;
  return false;
}
static void add_metrics(PPRendererMetrics *sum,PPRendererMetrics value) {
  sum->source_uploads+=value.source_uploads;sum->transform_draws+=value.transform_draws;sum->readbacks+=value.readbacks;
}
PPRendererMetrics pp_anchor_presenter_renderer_metrics(const PPAnchorPresenter *p) {
  if(!p) return (PPRendererMetrics){0};
  PPRendererMetrics sum=p->retired_renderer_metrics;add_metrics(&sum,pp_renderer_metrics(p->renderer));
  add_metrics(&sum,pp_renderer_metrics(p->texture_owner));
  for(size_t i=0;i<p->instance_count;i++) add_metrics(&sum,pp_renderer_metrics(p->instances[i].retained.renderer));
  return sum;
}
PPGpuResources pp_anchor_presenter_gpu_resources(const PPAnchorPresenter *p) {
  PPGpuResources result=p?p->gpu:(PPGpuResources){0};
  if(!result.limit_bytes) result.limit_bytes=PP_RETAINED_GPU_LIMIT;return result;
}
void pp_retained_release(PPAnchorPresenter *p,PPRetainedPresentation *r) {
  add_metrics(&p->retired_renderer_metrics,pp_renderer_metrics(r->renderer));
  pp_renderer_destroy(r->renderer);pp_item_snapshot_release(r->snapshot);
  pp_gif_release(p,r->gif_entry);
  p->gpu.retained_bytes-=r->gpu_bytes;memset(r,0,sizeof(*r));
}
void pp_sprite_release_presentation(PPAnchorPresenter *p,PPSprite *s) {
  pp_scalable_release(p,s);
  pp_retained_release(p,&s->retained);
  pp_assets_release_raster(p->assets,s->raster_bytes);s->raster_bytes=0;
}
void pp_sprite_hide(PPSprite *s) {pp_surface_hide(s->surface);s->retained.visible=false;}
static bool bounds(PPSourcePresentation source,PPPose pose,const PPDisplay *d,PPRect *out,PPError *error) {
  if(!isfinite(source.layout.x)||!isfinite(source.layout.y)||source.layout.x<=0||source.layout.y<=0 ||
     !isfinite(d->drawable_pixels_per_dip)||d->drawable_pixels_per_dip<=0 ||
     !isfinite(d->desktop_units_per_dip)||d->desktop_units_per_dip<=0)
    return fail(error,PP_ERROR_ARGUMENT,"Invalid canonical presentation layout or density");
  int w,h;
  if(!pp_geometry_source_extent(source.layout,source.source_rect,pose.longest_dip,
       pose.angle_deg,d->drawable_pixels_per_dip,&w,&h)||w>8192||h>8192)
    return fail(error,PP_ERROR_RESOURCE_LIMIT,"Presented raster exceeds 8192 pixels per axis");
  double native=d->desktop_units_per_dip/d->drawable_pixels_per_dip;
  *out=(PPRect){round(pose.center.x-w*native*.5),round(pose.center.y-h*native*.5),w*native,h*native};return true;
}
bool pp_retained_present(PPAnchorPresenter *p,PPSurface *surface,PPRetainedPresentation *r,
    PPSourcePresentation source,PPPose pose,const PPDisplay *display,PPError *error) {
  if(!source.source_rect.w && !source.source_rect.h) source.source_rect=(PPRect){0,0,source.layout.x,source.layout.y};
  PPRect rectangle;if(!bounds(source,pose,display,&rectangle,error)) return false;
  bool changed=source.content_id!=r->content_id || source.source_revision!=r->source_revision;
  if(changed) {
    const PPFrame *frame=source.frame;
    if(!frame || frame->width<=0 || frame->height<=0 || frame->width>8192 || frame->height>8192)
      return fail(error,PP_ERROR_ARGUMENT,"Changed retained source requires a validated frame");
    size_t candidate=(size_t)frame->width*frame->height*4;
    bool overlap=!source.shared_texture && (r->content_id!=source.content_id || r->source_width!=frame->width || r->source_height!=frame->height);
    if(!p->gpu.limit_bytes) p->gpu.limit_bytes=PP_RETAINED_GPU_LIMIT;
    if(overlap && (candidate>p->gpu.limit_bytes || p->gpu.retained_bytes>p->gpu.limit_bytes-candidate))
      return fail(error,PP_ERROR_RESOURCE_LIMIT,"Retained GPU sources exceed 256 MiB including candidate overlap");
    if(overlap) {
      p->gpu.retained_bytes+=candidate;
      if(p->gpu.retained_bytes>p->gpu.peak_bytes) p->gpu.peak_bytes=p->gpu.retained_bytes;
    }
    bool ok=r->renderer || (pp_presenter_texture_owner(p,error) && pp_renderer_create_shared_window(&r->renderer,pp_surface_window_id(surface),p->texture_owner,error));
    if(ok) ok=source.shared_texture?pp_renderer_bind_shared(r->renderer,source.shared_texture,source.content_id,source.source_revision,error):
        pp_renderer_upload_source(r->renderer,frame,source.content_id,source.source_revision,error);
    if(!ok) {
      if(overlap) p->gpu.retained_bytes-=candidate;
      /* A protective allocation rejection leaves the transactional old texture
       * usable. A broken context/presentation cannot keep intercepting input. */
      if(error && error->code==PP_ERROR_RENDER) goto native_failure;
      return false;
    }
    if(overlap) {p->gpu.retained_bytes-=r->gpu_bytes;r->gpu_bytes=candidate;}
    else if(source.shared_texture) {p->gpu.retained_bytes-=r->gpu_bytes;r->gpu_bytes=0;}
    r->source_width=frame->width;r->source_height=frame->height;
    r->content_id=source.content_id;r->source_revision=source.source_revision;
    r->visual_alpha=source.visual_alpha;
  }
  bool interactive=pose.opacity>0 && r->visual_alpha;
  bool redraw=changed || !r->valid || !r->visible || pose.longest_dip!=r->drawn.longest_dip ||
    pose.angle_deg!=r->drawn.angle_deg || pose.opacity!=r->drawn.opacity ||
    display->drawable_pixels_per_dip!=r->density || source.layout.x!=r->layout.x || source.layout.y!=r->layout.y ||
    memcmp(&source.source_rect,&r->source_rect,sizeof(source.source_rect)) ||
    rectangle.w!=r->bounds.w || rectangle.h!=r->bounds.h;
  bool place=!r->visible || memcmp(&rectangle,&r->bounds,sizeof(rectangle)) || !interactive;
  if(place && !pp_surface_place_gl(surface,rectangle,interactive,error)) goto native_failure;
  if(interactive && redraw && !pp_renderer_draw_source(r->renderer,source.layout,source.source_rect,pose.longest_dip,pose.angle_deg,
      pose.opacity,display->drawable_pixels_per_dip,error)) goto native_failure;
  if(place || (redraw && interactive)) p->metrics.native_publications++;
  r->drawn=pose;r->layout=source.layout;r->source_rect=source.source_rect;r->bounds=rectangle;
  r->density=display->drawable_pixels_per_dip;r->visible=interactive;r->valid=true;
  return true;
native_failure:
  pp_surface_hide(surface);p->failed_surface=surface;return false;
}
bool pp_sprite_present(PPAnchorPresenter *p,PPSprite *s,const PPSceneInstance *instance,
    const PPDisplay *display,double elapsed,double *next,PPError *error) {
#if defined(_WIN32) || defined(PP_TEST_DIRECT_TEXT)
  bool direct=pp_snapshot_is_svg(instance->snapshot)||pp_snapshot_is_png(instance->snapshot)||pp_snapshot_is_gif(instance->snapshot)||pp_item_snapshot_text_result(instance->snapshot)!=NULL;
#else
  bool direct=false;
#endif
  bool was_direct=pp_surface_window_id(s->surface)!=0;
  PPSurface *surface=s->surface;
  if(direct!=was_direct) {
    bool created=direct?pp_surface_create_gl(&surface,error):pp_surface_create(&surface,error);
    if(!created) return false;pp_surface_set_instance_id(surface,s->id);
  }
  bool ok;
  if(direct) {
    uint64_t identity=pp_item_snapshot_revision(instance->snapshot);
    PPSourcePresentation source={NULL,identity,1,pp_item_snapshot_size(instance->snapshot)};
    bool gif=pp_snapshot_is_gif(instance->snapshot);struct PPGifEntry *candidate=NULL;
    bool bitmap=pp_snapshot_is_bitmap_text(instance->snapshot);
    bool text=pp_snapshot_is_scalable(instance->snapshot);
    if(bitmap && identity==s->retained.content_id) source.source_rect=s->retained.source_rect;
    if(text&&!pp_scalable_source(p,s,instance,display,&source,error))ok=false;
    else if(bitmap && identity!=s->retained.content_id && !pp_retained_bitmap_text_source(instance->snapshot,&source,error))ok=false;
    else if(gif && !pp_gif_acquire(p,instance->snapshot,elapsed,&source,next,&candidate,error))ok=false;
    else if(!gif && !bitmap && !text && identity!=s->retained.content_id && !pp_item_snapshot_frame(instance->snapshot,elapsed,1,&source.frame,next,error)) ok=false;
    else {
      if(!gif && source.frame) source.visual_alpha=pp_source_visual_alpha(source.frame);
      if(text&&identity!=s->retained.content_id&&!source.frame) {pp_surface_hide(surface);ok=true;}
      else ok=pp_retained_present(p,surface,&s->retained,source,s->pose,display,error);
      if(!gif)*next=HUGE_VAL;
      if(ok) {pp_gif_release(p,s->retained.gif_entry);s->retained.gif_entry=candidate;candidate=NULL;}
    }
    pp_gif_release(p,candidate);
    if(!ok && error && error->code==PP_ERROR_RENDER) {
      pp_surface_hide(surface);p->failed_surface=surface;
    }
    if(ok) {
      if(!pp_snapshot_is_scalable(instance->snapshot)&&s->scalable_bytes)pp_scalable_release(p,s);
      pp_item_snapshot_retain(instance->snapshot);pp_item_snapshot_release(s->retained.snapshot);
      s->retained.snapshot=instance->snapshot;s->retained.animation_start=instance->animation_start;
    }
  } else {
    bool png=pp_snapshot_is_png(instance->snapshot),gif=pp_snapshot_is_gif(instance->snapshot),invisible=false;
    double hidden_next=HUGE_VAL;
    if(png || gif) {
      pp_surface_set_rectangular(surface,true);
      uint64_t identity=pp_item_snapshot_revision(instance->snapshot);
      if(gif || identity!=s->retained.content_id) {
        PPFrameSelection selected;
        if(!pp_item_snapshot_select_frame(instance->snapshot,elapsed,1,&selected,error)) return false;
        hidden_next=selected.next_change;
        if(identity!=s->retained.content_id || selected.revision!=s->retained.source_revision)
          s->retained.visual_alpha=pp_source_visual_alpha(selected.frame);
        s->retained.content_id=identity;s->retained.source_revision=selected.revision;
      }
      invisible=!s->retained.visual_alpha;
    }
    if(s->pose.opacity<=0 || invisible) {pp_surface_hide(surface);*next=hidden_next;ok=true;}
    else ok=pp_present_content(p,surface,instance->snapshot,s->pose,display,elapsed,next,&s->raster_bytes,error);
  }
  if(surface!=s->surface) {
    if(ok) {
      pp_surface_inherit_order(surface,s->surface);
      if(!direct) pp_retained_release(p,&s->retained);
      else {pp_assets_release_raster(p->assets,s->raster_bytes);s->raster_bytes=0;}
      pp_surface_destroy(s->surface);s->surface=surface;
    } else {
      if(direct) pp_retained_release(p,&s->retained);
      pp_surface_destroy(surface);if(p->failed_surface==surface) p->failed_surface=NULL;
    }
  }
  return ok;
}
