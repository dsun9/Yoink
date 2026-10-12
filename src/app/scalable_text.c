#include "presenter_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct TextCapture {
  PPTextStyle style;
  char *text, *family;
  uint8_t *primary, *fallback;
  size_t primary_bytes, fallback_bytes;
  double scale;
} TextCapture;
static void destroy_capture(void *data) {
  TextCapture *c=data;if(!c)return;
  free(c->text);free(c->family);free(c->primary);free(c->fallback);free(c);
}
static bool generate(void *data,size_t limit,PPTextResult *out,PPSourceMetrics *delta,PPError *error) {
  TextCapture *c=data;PPAssets *assets=NULL;bool ok=pp_assets_create(&assets,error);
  PPTextStyle style=c->style;style.requested_family=c->family;
  if(ok&&c->primary_bytes)ok=pp_asset_import(assets,(PPBytes){c->primary,c->primary_bytes},PP_ASSET_FONT,&style.primary_asset,error);
  if(ok&&c->fallback_bytes)ok=pp_asset_import(assets,(PPBytes){c->fallback,c->fallback_bytes},PP_ASSET_FONT,&style.fallback_asset,error);
  if(ok)ok=pp_text_render_result_bounded(assets,c->text,&style,c->scale,limit,out,NULL,error);
  *delta=pp_assets_source_metrics(assets);pp_assets_destroy(assets);return ok;
}
static bool copy_font(PPAssets *assets,PPAssetId id,uint8_t **out,size_t *bytes,PPError *error) {
  PPAsset *asset=pp_asset_retain(assets,id);
  if(!asset)return true;
  PPBytes source=pp_asset_source(asset);*bytes=source.size;
  if(source.size>16u*1024*1024) {pp_asset_release(asset);if(error)*error=(PPError){PP_ERROR_RESOURCE_LIMIT,"Worker font source exceeds 16 MiB"};return false;}
  *out=malloc(source.size);if(*out)memcpy(*out,source.data,source.size);pp_asset_release(asset);
  if(!*out&&error)*error=(PPError){PP_ERROR_MEMORY,"Cannot copy worker font bytes"};return *out!=NULL;
}
bool pp_snapshot_is_scalable_text(const PPItemSnapshot *snapshot) {
  const PPTextResult *text=pp_item_snapshot_text_result(snapshot);return text&&!text->artwork.bitmap_only;
}
bool pp_scalable_request(PPAnchorPresenter *p,PPSprite *s,const PPSceneInstance *instance,
    const PPDisplay *display,double longest,PPError *error) {
  if(!pp_snapshot_is_scalable_text(instance->snapshot))return true;
  PPVec2 layout=pp_item_snapshot_size(instance->snapshot);double scale=longest*display->drawable_pixels_per_dip/fmax(layout.x,layout.y);
  /* Conservative pre-reservation includes 32 destination pixels on each edge
   * for hinted glyph bounds. The bounded generator rejects a larger union
   * before allocating its final frame, preserving the available source. */
  double width=ceil(layout.x*scale)+64,height=ceil(layout.y*scale)+64;
  if(!isfinite(scale)||scale<=0||width>8192||height>8192||width*height>PP_WORKER_CANDIDATE_LIMIT/5.0) {
    if(error)*error=(PPError){PP_ERROR_RESOURCE_LIMIT,"Scalable target exceeds bounded worker raster limits"};return false;
  }
  const PPItem *item=pp_item_snapshot_item(instance->snapshot);
  if(strlen(item->text)>65536||strlen(item->text_style.requested_family)>4096) {
    if(error)*error=(PPError){PP_ERROR_RESOURCE_LIMIT,"Worker text or family exceeds capture limits"};return false;
  }
  TextCapture *c=calloc(1,sizeof(*c));if(!c) {if(error)*error=(PPError){PP_ERROR_MEMORY,"Cannot allocate immutable text capture"};return false;}
  c->style=item->text_style;c->scale=scale;
  size_t text_bytes=strlen(item->text)+1,family_bytes=strlen(item->text_style.requested_family)+1;
  c->text=malloc(text_bytes);c->family=malloc(family_bytes);
  if(c->text)memcpy(c->text,item->text,text_bytes);if(c->family)memcpy(c->family,item->text_style.requested_family,family_bytes);
  if(!c->text||!c->family) {if(error)*error=(PPError){PP_ERROR_MEMORY,"Cannot copy worker text or family"};destroy_capture(c);return false;}
  if(!copy_font(p->assets,c->style.primary_asset,&c->primary,&c->primary_bytes,error)||
      !copy_font(p->assets,c->style.fallback_asset,&c->fallback,&c->fallback_bytes,error)) {destroy_capture(c);return false;}
  c->style.requested_family=NULL;
  if(!p->worker&&!pp_raster_worker_create(&p->worker,error)) {destroy_capture(c);return false;}
  PPRasterKey key={p->generation,s->id,pp_item_snapshot_revision(instance->snapshot),++s->raster_serial,longest,display->drawable_pixels_per_dip};
  PPRasterCapture capture={c,sizeof(*c)+text_bytes+family_bytes+c->primary_bytes+c->fallback_bytes,generate,destroy_capture};
  if(!pp_raster_worker_request(p->worker,key,capture,p->assets,(size_t)width*(size_t)height*5,error))return false;
  s->raster_target=key;s->raster_requested=true;return true;
}
void pp_scalable_cancel(PPAnchorPresenter *p,PPSprite *s) {
  pp_raster_worker_cancel(p->worker,p->generation,s->id);s->raster_requested=false;s->raster_serial++;
}
void pp_scalable_release(PPAnchorPresenter *p,PPSprite *s) {
  pp_scalable_cancel(p,s);pp_frame_destroy(&s->scalable.frame);
  pp_assets_release_raster(p->assets,s->scalable_bytes);s->scalable_bytes=0;s->scalable=(PPTextResult){0};
}
static PPSourcePresentation packet(const PPTextResult *r,uint64_t identity,uint64_t revision) {
  return(PPSourcePresentation){.frame=&r->frame,.content_id=identity,.source_revision=revision,
    .layout=r->layout_size,.visual_alpha=pp_source_visual_alpha(&r->frame),
    .source_rect={r->layout_origin.x-r->raster_origin_px.x/r->raster_density,
      r->layout_origin.y-r->raster_origin_px.y/r->raster_density,
      r->frame.width/r->raster_density,r->frame.height/r->raster_density}};
}
bool pp_scalable_source(PPAnchorPresenter *p,PPSprite *s,const PPSceneInstance *instance,
    const PPDisplay *display,PPSourcePresentation *out,PPError *error) {
  uint64_t identity=pp_item_snapshot_revision(instance->snapshot);
  double target=s->resizing?s->ease_target.longest_dip:s->pose.longest_dip;
  bool current=s->scalable.frame.pixels&&s->scalable_identity==identity;
  bool matches=s->raster_target.content_revision==identity&&s->raster_target.longest_dip==target&&s->raster_target.density==display->drawable_pixels_per_dip;
  if(!matches) {
    if(!pp_scalable_request(p,s,instance,display,target,error))return false;
  }
  if(s->retained.content_id==identity) {
    *out=(PPSourcePresentation){.content_id=identity,.source_revision=s->retained.source_revision,
      .layout=s->retained.layout,.source_rect=s->retained.source_rect,.visual_alpha=s->retained.visual_alpha};
  } else {
    const PPTextResult *source=current?&s->scalable:pp_item_snapshot_text_result(instance->snapshot);
    *out=packet(source,identity,current?s->scalable_revision:1);
  }
  return true;
}
void pp_scalable_poll(PPAnchorPresenter *p) {
  PPRasterCompletion c={0};
  while(pp_raster_worker_poll(p->worker,p->generation,&c)) {
    PPSprite *s=pp_sprites_find(p,c.key.instance_id);
    const PPSceneInstance *instance=s?pp_scene_get(p->scene,s->id):NULL;
    const PPDisplay *d=NULL;
    if(s)for(size_t i=0;i<p->display_count;i++)if(p->displays[i].id==s->display_id)d=p->displays+i;
    bool valid=s&&instance&&d&&s->raster_requested&&s->raster_target.serial==c.key.serial&&
      pp_item_snapshot_revision(instance->snapshot)==c.key.content_revision&&d->drawable_pixels_per_dip==c.key.density&&
      (s->resizing?s->ease_target.longest_dip:s->pose.longest_dip)==c.key.longest_dip;
    if(!valid) {p->metrics.worker_stale_results+=c.success;pp_raster_completion_release(&c);continue;}
    s->raster_requested=false;
    if(!c.success) {
      p->notice=c.error;s->sharp_pending=false;pp_raster_completion_release(&c);continue;
    }
    PPError error={0};PPSourcePresentation source=packet(&c.result,c.key.content_revision,s->scalable_revision+2);
    if(!pp_retained_present(p,s->surface,&s->retained,source,s->pose,d,&error)) {
      p->notice=error;pp_raster_completion_release(&c);
      if(p->failed_surface==s->surface) {
        size_t index=(size_t)(s-p->instances);
        pp_surface_cancel_input(s->surface);pp_scene_delete(p->scene,s->id);
        pp_sprite_release_presentation(p,s);pp_surface_destroy(s->surface);pp_item_snapshot_release(s->seen_snapshot);
        memmove(s,s+1,(p->instance_count-index-1)*sizeof(*s));p->instance_count--;p->failed_surface=NULL;
      } else s->sharp_pending=false;
      continue;
    }
    size_t bytes=pp_raster_completion_take(&c);
    pp_frame_destroy(&s->scalable.frame);pp_assets_release_raster(p->assets,s->scalable_bytes);
    s->scalable=c.result;c.result=(PPTextResult){0};s->scalable_bytes=bytes;
    s->scalable_identity=c.key.content_revision;s->scalable_revision=source.source_revision;
    s->dirty=true;
    if(s->sharp_pending) {
      p->metrics.sharp_publications++;p->metrics.last_sharp_input_timestamp=s->resize_start;
      p->metrics.last_sharp_instance_id=s->id;p->metrics.last_sharp_content_revision=c.key.content_revision;
      p->metrics.last_sharp_longest_dip=c.key.longest_dip;p->metrics.last_sharp_drawable_density=c.key.density;s->sharp_pending=false;
    }
    pp_raster_completion_release(&c);
  }
}
