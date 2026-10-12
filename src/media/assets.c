#include "limits.h"
#include "pointy/sha256.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
bool pp_media_error(PPError *error,int code,const char *message) { if(error) { error->code=code; snprintf(error->message,sizeof(error->message),"%s",message); } return false; }
bool pp_media_size(int w,int h,size_t count,size_t *bytes,PPError *error) {
    if(w<=0 || h<=0 || !count) return pp_media_error(error,PP_ERROR_FORMAT,"Empty image dimensions or frame count");
    if(w>PP_MEDIA_MAX_DIMENSION || h>PP_MEDIA_MAX_DIMENSION || count>PP_MEDIA_MAX_FRAMES) return pp_media_error(error,PP_ERROR_RESOURCE_LIMIT,"Image exceeds 8192 pixels per axis or 4096 frames");
    size_t size=(size_t)w*h*5;
    if(count>PP_MEDIA_RESOURCE_BYTES/size) return pp_media_error(error,PP_ERROR_RESOURCE_LIMIT,"Decoded resource exceeds 256 MiB (RGBA and alpha)");
    *bytes=size*count; return true;
}
void pp_media_clear(PPAsset *a) {
    if(a->frames) { for(size_t i=0;i<a->info.count;i++) { free(a->frames[i].pixels); free(a->frames[i].content_alpha); } free(a->frames); }
    a->frames=NULL; a->owner->cache_bytes-=a->cache_bytes; a->cache_bytes=0;
}
bool pp_media_reserve(PPAsset *a,size_t bytes,PPError *error) {
    if(bytes>PP_MEDIA_RESOURCE_BYTES) return pp_media_error(error,PP_ERROR_RESOURCE_LIMIT,"Decoded resource exceeds 256 MiB");
    /* Account both old and new SVG raster until replacement succeeds. */
    for(PPAsset *other=a->owner->first;a->owner->cache_bytes+a->owner->external_bytes>PP_MEDIA_TOTAL_BYTES-bytes && other;other=other->next)
        if(other!=a && !other->retains) pp_media_clear(other);
    if(a->owner->cache_bytes+a->owner->external_bytes>PP_MEDIA_TOTAL_BYTES-bytes) return pp_media_error(error,PP_ERROR_RESOURCE_LIMIT,"Active decoded content exceeds 512 MiB raster budget");
    return true;
}
bool pp_media_allocate(PPAsset *a,size_t count,PPError *error) {
    size_t bytes; if(!pp_media_size(a->info.width,a->info.height,count,&bytes,error) || !pp_media_reserve(a,bytes,error)) return false;
    a->frames=calloc(count,sizeof(*a->frames)); if(!a->frames) return pp_media_error(error,PP_ERROR_MEMORY,"Could not allocate frame cache");
    a->cache_bytes=bytes; a->owner->cache_bytes+=bytes; return true;
}
bool pp_assets_create(PPAssets **out,PPError *error) { if(!out) return pp_media_error(error,PP_ERROR_ARGUMENT,"Missing asset manager output"); *out=calloc(1,sizeof(**out)); return *out!=NULL || pp_media_error(error,PP_ERROR_MEMORY,"Could not allocate asset manager"); }
void pp_assets_destroy(PPAssets *assets) { if(!assets) return; PPAsset *a=assets->first; while(a) { PPAsset *next=a->next; pp_media_clear(a); free(a->source); free(a); a=next; } free(assets); }
static bool decode(PPAsset *a,PPError *e) {
    bool ok;
    switch(a->kind) {
    case PP_ASSET_PNG: ok=pp_image_decode(a,e); break;
    case PP_ASSET_GIF: ok=pp_gif_decode(a,e); break;
    case PP_ASSET_SVG: return pp_svg_raster(a,a->info.width,a->info.height,e);
    default: return true;
    }
    if(ok) a->owner->metrics.raster_builds++;
    return ok;
}
PPSourceMetrics pp_assets_source_metrics(const PPAssets *assets) { return assets ? assets->metrics : (PPSourceMetrics){0}; }
bool pp_asset_import(PPAssets *assets,PPBytes b,PPAssetKind kind,PPAssetId *out,PPError *e) {
    if(!assets || !out || !b.data || !b.size || kind<PP_ASSET_PNG || kind>PP_ASSET_FONT) return pp_media_error(e,PP_ERROR_ARGUMENT,"Invalid asset import arguments");
    if(b.size>PP_MEDIA_RESOURCE_BYTES) return pp_media_error(e,PP_ERROR_RESOURCE_LIMIT,"Source resource exceeds 256 MiB");
    PPAssetId id; pp_sha256(b.data,b.size,id.bytes);
    for(PPAsset *a=assets->first;a;a=a->next) if(!memcmp(&a->id,&id,sizeof(id))) { if(a->kind!=kind) return pp_media_error(e,PP_ERROR_FORMAT,"Identical source already imported as a different asset kind"); *out=id; return true; }
    PPAsset *a=calloc(1,sizeof(*a)); if(!a) return pp_media_error(e,PP_ERROR_MEMORY,"Could not allocate asset"); a->owner=assets; a->id=id; a->kind=kind;
    bool valid=true;
    if(kind==PP_ASSET_PNG) valid=pp_image_preflight(b,&a->info,e);
    else if(kind==PP_ASSET_GIF) valid=pp_gif_preflight(b,&a->info,e);
    else if(kind==PP_ASSET_SVG) valid=pp_svg_preflight(b,&a->info,e);
    if(!valid) { free(a); return false; }
    a->source=malloc(b.size); if(!a->source) { free(a); return pp_media_error(e,PP_ERROR_MEMORY,"Could not own source bytes"); } memcpy(a->source,b.data,b.size); a->source_size=b.size;
    if(!decode(a,e)) { pp_media_clear(a); free(a->source); free(a); return false; }
    a->next=assets->first; assets->first=a; *out=id; return true;
}
PPAsset *pp_asset_retain(PPAssets *assets,PPAssetId id) { if(!assets) return NULL; for(PPAsset *a=assets->first;a;a=a->next) if(!memcmp(&a->id,&id,sizeof(id))) { a->retains++; return a; } return NULL; }
void pp_asset_release(PPAsset *a) { if(a && a->retains) a->retains--; }
PPBytes pp_asset_source(const PPAsset *a) { return a?(PPBytes){a->source,a->source_size}:(PPBytes){0}; }
PPAssetKind pp_asset_kind(const PPAsset *a) { return a ? a->kind : (PPAssetKind)-1; }
PPVec2 pp_asset_intrinsic_size(const PPAsset *a) { return a ? (PPVec2){a->info.width,a->info.height} : (PPVec2){0}; }
const PPFrame *pp_asset_cached_svg(const PPAsset *a) { return a && a->kind==PP_ASSET_SVG ? a->frames : NULL; }
size_t pp_assets_external_raster_bytes(const PPAssets *a) { return a ? a->external_bytes : 0; }
size_t pp_assets_raster_bytes(const PPAssets *a) { return a ? a->cache_bytes+a->external_bytes : 0; }
void pp_assets_trim_cache(PPAssets *assets) {
    for (PPAsset *asset = assets ? assets->first : NULL; asset; asset = asset->next)
        if (!asset->retains) pp_media_clear(asset);
}
bool pp_assets_reserve_raster(PPAssets *a, size_t bytes, PPError *error) {
    if (!a || !bytes) return pp_media_error(error, PP_ERROR_ARGUMENT, "Missing raster manager or bytes");
    if (bytes > PP_MEDIA_RESOURCE_BYTES)
        return pp_media_error(error, PP_ERROR_RESOURCE_LIMIT, "Owned raster exceeds 256 MiB");
    for (PPAsset *other = a->first; pp_assets_raster_bytes(a) > PP_MEDIA_TOTAL_BYTES-bytes && other; other = other->next)
        if (!other->retains) pp_media_clear(other);
    if (pp_assets_raster_bytes(a) > PP_MEDIA_TOTAL_BYTES-bytes)
        return pp_media_error(error, PP_ERROR_RESOURCE_LIMIT, "Active rasters exceed 512 MiB");
    a->external_bytes += bytes;
    return true;
}
void pp_assets_release_raster(PPAssets *a, size_t bytes) {
    if (a && bytes <= a->external_bytes) a->external_bytes -= bytes;
}
bool pp_asset_select_frame(PPAsset *a,double t,int w,int h,PPFrameSelection *selection,PPError *e) {
    if(selection) *selection=(PPFrameSelection){0};
    const PPFrame **out=selection?&selection->frame:NULL;
    double *next=selection?&selection->next_change:NULL;
    if(out) *out=NULL;
    if(!a || !out || !next || !isfinite(t) || t<0) return pp_media_error(e,PP_ERROR_ARGUMENT,"Invalid frame request or instance clock");
    if(a->kind==PP_ASSET_FONT) return pp_media_error(e,PP_ERROR_ARGUMENT,"Font bytes do not have an image frame");
    if(a->kind==PP_ASSET_SVG) { if(w==0 && h==0) { w=a->info.width;h=a->info.height; } if(w<=0 || h<=0) return pp_media_error(e,PP_ERROR_ARGUMENT,"Raster size must contain two positive dimensions");
        if(!a->frames || a->frames[0].width!=w || a->frames[0].height!=h) if(!pp_svg_raster(a,w,h,e)) return false;
    } else if(!a->frames && !decode(a,e)) { pp_media_clear(a); return false; }
    size_t index=0; *next=INFINITY;
    if(a->kind==PP_ASSET_GIF && a->info.count>1) {
        if(a->plays && t>=a->duration*a->plays) index=a->info.count-1;
        else { double local=fmod(t,a->duration),boundary=0; index=a->info.count-1;
            for(size_t i=0;i<a->info.count;i++) { boundary+=a->info.delays[i]; if(local<boundary) { index=i;break; } }
            *next=t+(boundary-local);
        }
    }
    *out=&a->frames[index]; selection->index=index;selection->revision=index+1;return true;
}
bool pp_asset_frame(PPAsset *a,double t,int w,int h,const PPFrame **out,double *next,PPError *e) {
    if(out) *out=NULL;
    if(!out || !next) return pp_media_error(e,PP_ERROR_ARGUMENT,"Invalid frame output");
    PPFrameSelection selection;
    if(!pp_asset_select_frame(a,t,w,h,&selection,e)) return false;
    *out=selection.frame;*next=selection.next_change;return true;
}
