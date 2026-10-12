#include "presenter_internal.h"
#include <stdlib.h>
#include <string.h>
struct PPGifEntry {
    struct PPGifEntry *next;
    PPAssetId asset;
    uint64_t revision;
    size_t refs,bytes;
    PPSharedTexture *texture;
    bool visual_alpha;
};
bool pp_snapshot_is_gif(const PPItemSnapshot *snapshot) {
    const PPItem *item=pp_item_snapshot_item(snapshot);
    if(!item || item->kind!=PP_ITEM_IMAGE)return false;
    PPAsset *asset=pp_asset_retain(pp_item_snapshot_assets(snapshot),item->image_asset);
    bool gif=asset && pp_asset_kind(asset)==PP_ASSET_GIF;pp_asset_release(asset);return gif;
}
bool pp_presenter_texture_owner(PPAnchorPresenter *p,PPError *error) {
    return p->texture_owner || pp_renderer_create_texture_owner(&p->texture_owner,error);
}
void pp_gif_release(PPAnchorPresenter *p,struct PPGifEntry *entry) {
    if(!entry || --entry->refs)return;
    struct PPGifEntry **link=&p->gif_entries;while(*link && *link!=entry)link=&(*link)->next;
    if(*link)*link=entry->next;
    pp_shared_texture_destroy(entry->texture);pp_presenter_gpu_release(p,entry->bytes);free(entry);
}
void pp_gif_clear(PPAnchorPresenter *p) {
    while(p->gif_entries) {p->gif_entries->refs=1;pp_gif_release(p,p->gif_entries);}
    PPRendererMetrics m=pp_renderer_metrics(p->texture_owner);
    p->retired_renderer_metrics.source_uploads+=m.source_uploads;
    p->retired_renderer_metrics.transform_draws+=m.transform_draws;
    p->retired_renderer_metrics.readbacks+=m.readbacks;
    pp_renderer_destroy(p->texture_owner);p->texture_owner=NULL;
    pp_presenter_gpu_release(p,p->retired_gpu_bytes);p->retired_gpu_bytes=0;
}
bool pp_gif_acquire(PPAnchorPresenter *p,PPItemSnapshot *snapshot,double elapsed,PPSourcePresentation *source,double *next,struct PPGifEntry **out,PPError *error) {
    *out=NULL;PPFrameSelection selected;
    if(!pp_item_snapshot_select_frame(snapshot,elapsed,1,&selected,error))return false;
    const PPItem *item=pp_item_snapshot_item(snapshot);struct PPGifEntry *entry=p->gif_entries;
    while(entry && (entry->revision!=selected.revision || memcmp(&entry->asset,&item->image_asset,sizeof(entry->asset))))entry=entry->next;
    if(!entry) {
        size_t bytes=(size_t)selected.frame->width*selected.frame->height*4;
        if(!pp_presenter_gpu_reserve(p,bytes,error))return false;
        entry=calloc(1,sizeof(*entry));if(!entry){pp_presenter_gpu_release(p,bytes);if(error)*error=(PPError){PP_ERROR_MEMORY,"Cannot own shared GIF frame"};return false;}
        if(!pp_presenter_texture_owner(p,error) || !pp_shared_texture_create(p->texture_owner,selected.frame,&entry->texture,error)){pp_presenter_gpu_release(p,bytes);free(entry);return false;}
        entry->asset=item->image_asset;entry->revision=selected.revision;entry->bytes=bytes;entry->visual_alpha=pp_source_visual_alpha(selected.frame);
        entry->next=p->gif_entries;p->gif_entries=entry;
    }
    entry->refs++;*out=entry;*next=selected.next_change;
    *source=(PPSourcePresentation){.frame=selected.frame,.content_id=pp_item_snapshot_content_id(snapshot),.source_revision=selected.revision,
        .layout=pp_item_snapshot_size(snapshot),.visual_alpha=entry->visual_alpha,.shared_texture=entry->texture};
    return true;
}
