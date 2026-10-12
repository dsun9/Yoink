#include "pointy/model.h"
#include "pointy/sha256.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PPItemSnapshot {
    PPAssets *assets;
    PPItemSnapshot *parent;
    PPAssetId artwork;
    size_t references;
    uint64_t revision;
    PPItem item;
    PPAsset *image, *primary, *fallback;
    PPVec2 size;
    PPTextResult text_result;
    PPTextDiagnostics diagnostics;
    PPTextResult compatibility;
    PPTextResult bitmap_source;
    double text_scale;
    size_t raster_bytes, bitmap_bytes,compatibility_bytes;
};
static uint64_t next_revision;
static PPAssetId artwork_id(const PPItem *item) {
    struct {PPAssetId source,primary,fallback;int kind,primary_face,fallback_face;uint8_t color[4];} key={0};
    key.kind=item->kind;
    if(item->kind==PP_ITEM_IMAGE)key.source=item->image_asset;
    else {
        if(item->text)pp_sha256((const uint8_t *)item->text,strlen(item->text),key.source.bytes);
        key.primary=item->text_style.primary_asset;key.fallback=item->text_style.fallback_asset;
        key.primary_face=item->text_style.primary_face;key.fallback_face=item->text_style.fallback_face;
        memcpy(key.color,item->text_style.color,4);
    }
    PPAssetId id;pp_sha256((const uint8_t *)&key,sizeof(key),id.bytes);return id;
}
static PPItemSnapshot *content_owner(PPItemSnapshot *snapshot){while(snapshot&&snapshot->parent)snapshot=snapshot->parent;return snapshot;}

static bool item_error(PPError *error, int code, const char *message) {
    if (error) {
        error->code = code;
        snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return false;
}

static bool valid_size(double value) {
    return isfinite(value) && value >= PP_ITEM_MIN_SIZE && value <= PP_ITEM_MAX_SIZE;
}

static char *copy_string(const char *value) {
    if (!value) return NULL;
    size_t bytes = strlen(value) + 1;
    char *copy = malloc(bytes);
    if (copy) memcpy(copy, value, bytes);
    return copy;
}

static bool retain_font(PPAssets *assets, PPAssetId id, int face, PPAsset **out, PPError *error) {
    PPAssetId absent = {0};
    if (!memcmp(&id, &absent, sizeof(id))) {
        if (face != 0) return item_error(error, PP_ERROR_ARGUMENT, "Absent font must use face zero");
        return true;
    }
    *out = pp_asset_retain(assets, id);
    if (!*out || pp_asset_kind(*out) != PP_ASSET_FONT)
        return item_error(error, PP_ERROR_ARGUMENT, "Font reference is missing or is not a font");
    PPFontInfo info;
    return pp_font_inspect(pp_asset_source(*out), face, &info, error);
}

static bool render_text(PPItemSnapshot *snapshot, double scale, PPError *error) {
    PPTextResult candidate = {0};
    if (!pp_text_render_result(snapshot->assets, snapshot->item.text, &snapshot->item.text_style,
            scale, &candidate, snapshot->text_result.frame.pixels?NULL:&snapshot->diagnostics, error)) return false;
    /* Text validates per-frame dimensions/bytes before allocating. The fresh
     * output is transient until reserved here; old retained pixels remain
     * charged until commit, so failure preserves the existing cache. */
    size_t bytes = candidate.frame.stride * (size_t)candidate.frame.height +
        (size_t)candidate.frame.width * candidate.frame.height;
    if (!pp_assets_reserve_raster(snapshot->assets, bytes, error)) {
        pp_frame_destroy(&candidate.frame);
        return false;
    }
    if(snapshot->text_result.frame.pixels) {
        pp_frame_destroy(&snapshot->compatibility.frame);
        pp_assets_release_raster(snapshot->assets,snapshot->compatibility_bytes);
        snapshot->compatibility=candidate;snapshot->compatibility_bytes=bytes;
    } else {snapshot->text_result=candidate;snapshot->raster_bytes=bytes;}
    snapshot->text_scale = scale;
    return true;
}

bool pp_item_snapshot_create(PPAssets *assets, const PPItem *item, PPItemSnapshot **out, PPError *error) {
    if (out) *out = NULL;
    if (!assets || !item || !out || !isfinite(item->angle_deg) ||
            !isfinite(item->opacity) || item->opacity < 0 || item->opacity > 1)
        return item_error(error, PP_ERROR_ARGUMENT, "Invalid item appearance or output");
    if (item->kind != PP_ITEM_IMAGE && item->kind != PP_ITEM_TEXT)
        return item_error(error, PP_ERROR_ARGUMENT, "Invalid item content kind");
    if (!valid_size(item->kind == PP_ITEM_IMAGE ? item->image_longest_dip : item->text_style.font_size_dip))
        return item_error(error, PP_ERROR_ARGUMENT, "Image longest side and font size must be within 1–4096 DIP");
    PPItemSnapshot *snapshot = calloc(1, sizeof(*snapshot));
    if (!snapshot) return item_error(error, PP_ERROR_MEMORY, "Cannot allocate item snapshot");
    snapshot->assets = assets;
    snapshot->references = 1;
    snapshot->item = *item;
    snapshot->item.text = NULL;
    snapshot->item.text_style.requested_family = NULL;
    if (item->kind == PP_ITEM_IMAGE) {
        snapshot->image = pp_asset_retain(assets, item->image_asset);
        if (!snapshot->image || pp_asset_kind(snapshot->image) == PP_ASSET_FONT) {
            item_error(error, PP_ERROR_ARGUMENT, "Image reference is missing or is not an image");
            goto failed;
        }
        PPVec2 intrinsic = pp_asset_intrinsic_size(snapshot->image);
        double factor = item->image_longest_dip / fmax(intrinsic.x, intrinsic.y);
        snapshot->size = (PPVec2){intrinsic.x * factor, intrinsic.y * factor};
    } else {
        if (!item->text || !item->text_style.requested_family || !*item->text_style.requested_family) {
            item_error(error, PP_ERROR_ARGUMENT, "Text and requested font family are required");
            goto failed;
        }
        snapshot->item.text = copy_string(item->text);
        snapshot->item.text_style.requested_family = copy_string(item->text_style.requested_family);
        if (!snapshot->item.text || !snapshot->item.text_style.requested_family) {
            item_error(error, PP_ERROR_MEMORY, "Cannot own text strings");
            goto failed;
        }
        if (!retain_font(assets, item->text_style.primary_asset, item->text_style.primary_face,
                &snapshot->primary, error) ||
            !retain_font(assets, item->text_style.fallback_asset, item->text_style.fallback_face,
                &snapshot->fallback, error) || !render_text(snapshot, 1, error)) goto failed;
        snapshot->size = snapshot->text_result.layout_size;
    }
    snapshot->artwork=artwork_id(item);
    snapshot->revision = ++next_revision;
    *out = snapshot;
    return true;
failed:
    pp_item_snapshot_release(snapshot);
    return false;
}
bool pp_item_snapshot_update(PPItemSnapshot *previous,const PPItem *item,PPItemSnapshot **out,PPError *error) {
    if(out)*out=NULL;
    if(!previous||!item||!out)return item_error(error,PP_ERROR_ARGUMENT,"Snapshot update requires previous content");
    const PPItem *old=&previous->item;PPAssetId identity=artwork_id(item);
    bool same=item->kind==old->kind&&!memcmp(&identity,&previous->artwork,sizeof(identity));
    if(item->kind==PP_ITEM_TEXT)same=same&&item->text_style.font_size_dip==old->text_style.font_size_dip&&
        item->text_style.requested_family&&old->text_style.requested_family&&!strcmp(item->text_style.requested_family,old->text_style.requested_family)&&
        item->text_style.default_emoji_fallback==old->text_style.default_emoji_fallback;
    if(!same)return pp_item_snapshot_create(previous->assets,item,out,error);
    if(!isfinite(item->angle_deg)||!isfinite(item->opacity)||item->opacity<0||item->opacity>1||
        !valid_size(item->kind==PP_ITEM_IMAGE?item->image_longest_dip:item->text_style.font_size_dip))return item_error(error,PP_ERROR_ARGUMENT,"Invalid item appearance");
    PPItemSnapshot *snapshot=calloc(1,sizeof(*snapshot));if(!snapshot)return item_error(error,PP_ERROR_MEMORY,"Cannot retain item appearance");
    PPItemSnapshot *owner=content_owner(previous);snapshot->assets=previous->assets;snapshot->references=1;
    snapshot->parent=pp_item_snapshot_retain(owner);snapshot->item=*item;snapshot->artwork=identity;snapshot->revision=++next_revision;
    snapshot->item.text=owner->item.text;snapshot->item.text_style.requested_family=owner->item.text_style.requested_family;snapshot->size=previous->size;
    if(item->kind==PP_ITEM_IMAGE){PPVec2 intrinsic=pp_asset_intrinsic_size(owner->image);double factor=item->image_longest_dip/fmax(intrinsic.x,intrinsic.y);snapshot->size=(PPVec2){intrinsic.x*factor,intrinsic.y*factor};}
    *out=snapshot;return true;
}

PPItemSnapshot *pp_item_snapshot_retain(PPItemSnapshot *snapshot) {
    if (snapshot) snapshot->references++;
    return snapshot;
}

void pp_item_snapshot_release(PPItemSnapshot *snapshot) {
    if (!snapshot || --snapshot->references) return;
    if(snapshot->parent){pp_item_snapshot_release(snapshot->parent);free(snapshot);return;}
    pp_frame_destroy(&snapshot->text_result.frame);
    pp_frame_destroy(&snapshot->compatibility.frame);
    pp_frame_destroy(&snapshot->bitmap_source.frame);
    pp_assets_release_raster(snapshot->assets, snapshot->raster_bytes);
    pp_assets_release_raster(snapshot->assets, snapshot->bitmap_bytes);
    pp_assets_release_raster(snapshot->assets,snapshot->compatibility_bytes);
    pp_asset_release(snapshot->image);
    pp_asset_release(snapshot->primary);
    pp_asset_release(snapshot->fallback);
    free((void *)snapshot->item.text);
    free((void *)snapshot->item.text_style.requested_family);
    free(snapshot);
}

const PPItem *pp_item_snapshot_item(const PPItemSnapshot *snapshot) {
    return snapshot ? &snapshot->item : NULL;
}
PPVec2 pp_item_snapshot_size(const PPItemSnapshot *snapshot) {
    return snapshot ? snapshot->size : (PPVec2){0};
}
PPAssets *pp_item_snapshot_assets(const PPItemSnapshot *snapshot) {
    return snapshot ? snapshot->assets : NULL;
}
uint64_t pp_item_snapshot_revision(const PPItemSnapshot *snapshot) {
    return snapshot ? snapshot->revision : 0;
}
PPAssetId pp_item_snapshot_artwork_id(const PPItemSnapshot *snapshot){return snapshot?snapshot->artwork:(PPAssetId){0};}
uint64_t pp_item_snapshot_content_id(const PPItemSnapshot *snapshot){uint64_t value=0;if(snapshot)memcpy(&value,snapshot->artwork.bytes,sizeof(value));return value?value:1;}
const PPTextResult *pp_item_snapshot_text_result(const PPItemSnapshot *snapshot) {
    snapshot=content_owner((PPItemSnapshot *)snapshot);
    return snapshot && snapshot->item.kind==PP_ITEM_TEXT?&snapshot->text_result:NULL;
}
const PPTextDiagnostics *pp_item_snapshot_text_diagnostics(const PPItemSnapshot *snapshot) {
    snapshot=content_owner((PPItemSnapshot *)snapshot);
    return snapshot&&snapshot->item.kind==PP_ITEM_TEXT?&snapshot->diagnostics:NULL;
}
bool pp_item_snapshot_bitmap_source(PPItemSnapshot *snapshot,const PPTextResult **out,PPError *error) {
    snapshot=content_owner(snapshot);
    if(out) *out=NULL;
    if(!snapshot || !out || snapshot->item.kind!=PP_ITEM_TEXT || !snapshot->text_result.artwork.bitmap_only)
        return item_error(error,PP_ERROR_ARGUMENT,"Useful bitmap source requires completely bitmap text artwork");
    if(!snapshot->bitmap_source.frame.pixels) {
        const size_t total_limit=512u*1024u*1024u,resource_limit=256u*1024u*1024u;
        size_t retained=pp_assets_raster_bytes(snapshot->assets);
        if(retained>=total_limit)
            return item_error(error,PP_ERROR_RESOURCE_LIMIT,"Active rasters exceed 512 MiB including bitmap candidate");
        size_t available=total_limit-retained;
        if(available>resource_limit) available=resource_limit;
        PPTextResult candidate={0};
        /* Preflight includes temporary source coverage and keeps every old
         * retained frame charged. The text pipeline validates visible alpha
         * before this presentation-only source drops its coverage plane. */
        if(!pp_text_render_result_bounded(snapshot->assets,snapshot->item.text,&snapshot->item.text_style,
            snapshot->text_result.artwork.useful_pixels_per_dip,available,&candidate,NULL,error)) return false;
        free(candidate.frame.content_alpha);candidate.frame.content_alpha=NULL;
        size_t bytes=candidate.frame.stride*(size_t)candidate.frame.height;
        if(!pp_assets_reserve_raster(snapshot->assets,bytes,error)) {
            pp_frame_destroy(&candidate.frame);return false;
        }
        snapshot->bitmap_source=candidate;snapshot->bitmap_bytes=bytes;
    }
    *out=&snapshot->bitmap_source;return true;
}

bool pp_item_snapshot_frame(PPItemSnapshot *snapshot, double elapsed, double scale,
    const PPFrame **out, double *next, PPError *error) {
    if (out) *out = NULL;
    if (!snapshot || !out || !next || !isfinite(elapsed) || elapsed < 0 || !isfinite(scale) || scale <= 0)
        return item_error(error, PP_ERROR_ARGUMENT, "Invalid snapshot frame clock or scale");
    double width = ceil(snapshot->size.x * scale), height = ceil(snapshot->size.y * scale);
    if (!isfinite(width) || !isfinite(height) || width > 8192 || height > 8192)
        return item_error(error, PP_ERROR_RESOURCE_LIMIT, "Raster exceeds 8192 pixels per axis");
    if (snapshot->item.kind == PP_ITEM_IMAGE)
        return pp_asset_frame(content_owner(snapshot)->image, elapsed, (int)fmax(1, width), (int)fmax(1, height), out, next, error);
    snapshot=content_owner(snapshot);
    if(scale==1)*out=&snapshot->text_result.frame;
    else {
        if((snapshot->text_scale!=scale||!snapshot->compatibility.frame.pixels)&&!render_text(snapshot,scale,error))return false;
        *out=&snapshot->compatibility.frame;
    }
    *next = INFINITY;
    return true;
}
bool pp_item_snapshot_select_frame(PPItemSnapshot *snapshot,double elapsed,double scale,
    PPFrameSelection *out,PPError *error) {
    if(out) *out=(PPFrameSelection){0};
    if(!snapshot || !out) return item_error(error,PP_ERROR_ARGUMENT,"Missing frame selection output");
    if(snapshot->item.kind==PP_ITEM_IMAGE) {
        if(!isfinite(elapsed) || elapsed<0 || !isfinite(scale) || scale<=0)
            return item_error(error,PP_ERROR_ARGUMENT,"Invalid frame clock or scale");
        double w=ceil(snapshot->size.x*scale),h=ceil(snapshot->size.y*scale);
        if(!isfinite(w)||!isfinite(h)||w>8192||h>8192)
            return item_error(error,PP_ERROR_RESOURCE_LIMIT,"Raster exceeds 8192 pixels per axis");
        return pp_asset_select_frame(content_owner(snapshot)->image,elapsed,(int)fmax(1,w),(int)fmax(1,h),out,error);
    }
    if(!pp_item_snapshot_frame(snapshot,elapsed,scale,&out->frame,&out->next_change,error)) return false;
    out->revision=1;return true;
}
