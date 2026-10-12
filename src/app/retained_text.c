#include "presenter_internal.h"

bool pp_snapshot_is_bitmap_text(const PPItemSnapshot *snapshot) {
  const PPTextResult *result=pp_item_snapshot_text_result(snapshot);
  return result && result->artwork.bitmap_only;
}

bool pp_retained_bitmap_text_source(PPItemSnapshot *snapshot,PPSourcePresentation *source,PPError *error) {
  const PPTextResult *result=NULL;
  if(!pp_item_snapshot_bitmap_source(snapshot,&result,error)) return false;
  double density=result->raster_density;
  *source=(PPSourcePresentation){.frame=&result->frame,
    .content_id=pp_item_snapshot_revision(snapshot),.source_revision=1,
    .layout=result->layout_size,.visual_alpha=pp_source_visual_alpha(&result->frame),
    .source_rect={result->layout_origin.x-result->raster_origin_px.x/density,
      result->layout_origin.y-result->raster_origin_px.y/density,
      result->frame.width/density,result->frame.height/density}};
  return true;
}
