#ifndef POINTY_ASSETS_H
#define POINTY_ASSETS_H
#include "pointy/render.h"
typedef struct PPBytes { const uint8_t *data; size_t size; } PPBytes;
typedef struct PPAssetId { uint8_t bytes[32]; } PPAssetId;
typedef enum PPAssetKind { PP_ASSET_PNG, PP_ASSET_GIF, PP_ASSET_SVG, PP_ASSET_FONT } PPAssetKind;
enum { PP_ERROR_FORMAT = 5, PP_ERROR_RESOURCE_LIMIT };
typedef struct PPAssets PPAssets;
typedef struct PPAsset PPAsset;
/* Single-threaded manager; it must outlive every retained asset. Projects borrow
 * the app-owned manager. Imports copy the source bytes; IDs hash only those bytes.
 * Limits: 8192 per axis, 4096 GIF frames, 256 MiB decoded per resource, 512 MiB
 * total asset decoded/raster cache, and 256 MiB source bytes per import.
 * Budget counts RGBA + content_alpha planes in this manager. Source bytes,
 * decoder scratch, SVG document scratch, and separately owned text/presenter
 * frames are outside it. Runtime must budget those owned raster frames too. */
bool pp_assets_create(PPAssets **out, PPError *error);
void pp_assets_destroy(PPAssets *assets);
bool pp_asset_import(PPAssets *assets, PPBytes bytes, PPAssetKind kind, PPAssetId *out, PPError *error);
PPAsset *pp_asset_retain(PPAssets *assets, PPAssetId id);
void pp_asset_release(PPAsset *asset);
/* Source remains valid until manager destruction, including cache eviction. */
PPBytes pp_asset_source(const PPAsset *asset);
/* Borrows cache: retain asset during use. PNG/GIF frames remain valid while any
 * retain exists. SVG has one raster: a successful different-size request on the
 * same asset invalidates the previous frame, including for other retainers.
 * Consumers copy/transform/present before rerasterizing, or request again.
 * Unretained caches may be evicted by any import/frame request. Failed requests
 * preserve the existing cache and set *out=NULL. Zero dimensions select intrinsic
 * SVG dimensions; PNG/GIF ignore raster dimensions. FONT has no image frame.
 * elapsed_seconds is a nonnegative instance clock; next_change_seconds is an
 * absolute deadline in that clock, INFINITY for static/exhausted animations. */
bool pp_asset_frame(PPAsset *asset, double elapsed_seconds, int raster_width, int raster_height,
    const PPFrame **out, double *next_change_seconds, PPError *error);
#endif
