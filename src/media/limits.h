#ifndef POINTY_MEDIA_LIMITS_H
#define POINTY_MEDIA_LIMITS_H
#include "pointy/assets.h"
#include <SDL3/SDL.h>
#define PP_MEDIA_MAX_DIMENSION 8192
#define PP_MEDIA_MAX_FRAMES 4096
#define PP_MEDIA_RESOURCE_BYTES ((size_t)256*1024*1024)
#define PP_MEDIA_TOTAL_BYTES ((size_t)512*1024*1024)
typedef struct PPMediaInfo { int width,height; size_t count; double delays[PP_MEDIA_MAX_FRAMES]; PPVec2 svg_viewport; } PPMediaInfo;
struct PPAsset {
    PPAssets *owner; struct PPAsset *next;
    PPAssetId id; PPAssetKind kind; uint8_t *source; size_t source_size;
    unsigned retains; PPMediaInfo info; PPFrame *frames;
    size_t cache_bytes; double duration; uint64_t plays;
};
struct PPAssets { PPAsset *first; size_t cache_bytes, external_bytes; PPSourceMetrics metrics; };
bool pp_media_error(PPError *error,int code,const char *message);
bool pp_media_size(int width,int height,size_t count,size_t *bytes,PPError *error);
bool pp_media_reserve(PPAsset *asset,size_t bytes,PPError *error);
void pp_media_clear(PPAsset *asset);
bool pp_media_allocate(PPAsset *asset,size_t count,PPError *error);
bool pp_image_preflight(PPBytes bytes,PPMediaInfo *info,PPError *error);
bool pp_image_decode(PPAsset *asset,PPError *error);
bool pp_image_copy(SDL_Surface *surface,PPFrame *frame,PPError *error);
bool pp_gif_preflight(PPBytes bytes,PPMediaInfo *info,PPError *error);
bool pp_gif_decode(PPAsset *asset,PPError *error);
bool pp_svg_validate(PPBytes bytes,PPError *error);
bool pp_svg_preflight(PPBytes bytes,PPMediaInfo *info,PPError *error);
bool pp_svg_raster(PPAsset *asset,int width,int height,PPError *error);
#endif
