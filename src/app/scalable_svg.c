#include "presenter_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct SvgCapture {
  uint8_t *bytes;
  size_t size;
  int width,height;
  PPVec2 layout;
  double density;
} SvgCapture;
static void destroy_capture(void *data) {
  SvgCapture *c=data;if(c){free(c->bytes);free(c);}
}
static bool generate(void *data,size_t limit,PPTextResult *out,PPSourceMetrics *delta,PPError *error) {
  SvgCapture *c=data;
  if(!pp_svg_render_bounded((PPBytes){c->bytes,c->size},c->width,c->height,limit,PP_WORKER_SCRATCH_LIMIT,&out->frame,error))return false;
  out->layout_size=c->layout;out->raster_density=c->density;delta->raster_builds=1;return true;
}
bool pp_snapshot_is_svg(const PPItemSnapshot *snapshot) {
  const PPItem *item=pp_item_snapshot_item(snapshot);
  if(!item||item->kind!=PP_ITEM_IMAGE)return false;
  PPAsset *asset=pp_asset_retain(pp_item_snapshot_assets(snapshot),item->image_asset);
  bool svg=asset&&pp_asset_kind(asset)==PP_ASSET_SVG;pp_asset_release(asset);return svg;
}
bool pp_snapshot_is_scalable(const PPItemSnapshot *snapshot) {
  return pp_snapshot_is_scalable_text(snapshot)||pp_snapshot_is_svg(snapshot);
}
bool pp_svg_request(PPAnchorPresenter *p,PPSprite *s,const PPSceneInstance *instance,
    const PPDisplay *display,double longest,PPError *error) {
  PPVec2 layout=pp_item_snapshot_size(instance->snapshot);
  double scale=longest*display->drawable_pixels_per_dip/fmax(layout.x,layout.y);
  double w=ceil(layout.x*scale),h=ceil(layout.y*scale);
  if(!isfinite(scale)||scale<=0||w<1||h<1||w>8192||h>8192||w*h>PP_WORKER_CANDIDATE_LIMIT/5.0) {
    if(error)*error=(PPError){PP_ERROR_RESOURCE_LIMIT,"SVG target exceeds bounded worker raster limits"};return false;
  }
  PPAsset *asset=pp_asset_retain(p->assets,pp_item_snapshot_item(instance->snapshot)->image_asset);
  PPBytes source=pp_asset_source(asset);
  if(!source.size||source.size>32u*1024*1024-sizeof(SvgCapture)) {
    pp_asset_release(asset);if(error)*error=(PPError){PP_ERROR_RESOURCE_LIMIT,"SVG source exceeds worker capture limit"};return false;
  }
  if(pp_scalable_defer_if_busy(p,s,instance,display,longest)) {pp_asset_release(asset);return true;}
  SvgCapture *c=calloc(1,sizeof(*c));if(c)c->bytes=malloc(source.size);
  if(!c||!c->bytes) {destroy_capture(c);pp_asset_release(asset);if(error)*error=(PPError){PP_ERROR_MEMORY,"Cannot copy immutable SVG source"};return false;}
  memcpy(c->bytes,source.data,source.size);c->size=source.size;c->width=(int)w;c->height=(int)h;c->layout=layout;c->density=scale;
  pp_asset_release(asset);
  if(!p->worker&&!pp_raster_worker_create(&p->worker,error)) {destroy_capture(c);return false;}
  PPRasterKey key={p->generation,s->id,pp_item_snapshot_revision(instance->snapshot),++s->raster_serial,longest,display->drawable_pixels_per_dip};
  PPRasterCapture capture={c,sizeof(*c)+c->size,generate,destroy_capture};
  if(!pp_raster_worker_request(p->worker,key,capture,p->assets,(size_t)w*(size_t)h*5,error))return false;
  s->raster_target=key;s->raster_requested=true;s->raster_retry=false;return true;
}
