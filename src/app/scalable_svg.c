#include "presenter_internal.h"
bool pp_snapshot_is_svg(const PPItemSnapshot *snapshot) {
  const PPItem *item=pp_item_snapshot_item(snapshot);if(!item||item->kind!=PP_ITEM_IMAGE)return false;
  PPAsset *asset=pp_asset_retain(pp_item_snapshot_assets(snapshot),item->image_asset);
  bool svg=asset&&pp_asset_kind(asset)==PP_ASSET_SVG;pp_asset_release(asset);return svg;
}
bool pp_snapshot_is_scalable(const PPItemSnapshot *snapshot){return pp_snapshot_is_scalable_text(snapshot)||pp_snapshot_is_svg(snapshot);}
bool pp_svg_request(PPAnchorPresenter *p,PPSprite *s,const PPSceneInstance *instance,const PPDisplay *display,double longest,PPError *error) {
  return pp_scalable_request(p,s,instance,display,longest,error);
}
