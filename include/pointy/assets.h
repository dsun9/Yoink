#ifndef POINTY_ASSETS_H
#define POINTY_ASSETS_H
#include "pointy/render.h"
typedef struct PPBytes { const uint8_t *data; size_t size; } PPBytes;
typedef struct PPAssetId { uint8_t bytes[32]; } PPAssetId;
typedef enum PPAssetKind { PP_ASSET_PNG, PP_ASSET_GIF, PP_ASSET_SVG, PP_ASSET_FONT } PPAssetKind;
enum { PP_ERROR_FORMAT = 5, PP_ERROR_RESOURCE_LIMIT };
typedef struct PPAssets PPAssets;
typedef struct PPAsset PPAsset;
typedef struct PPFrameSelection {
    const PPFrame *frame;
    size_t index;
    uint64_t revision; /* Selected immutable frame index + 1, scoped to asset ID. */
    double next_change; /* Absolute deadline in the caller's elapsed clock. */
} PPFrameSelection;
/* No shared playback state. PNG/GIF frames retain native resolution; repeated
 * frames and loop revisits preserve their revision, including finite stop. */
bool pp_asset_select_frame(PPAsset *asset,double elapsed_seconds,int width,int height,
    PPFrameSelection *out,PPError *error);
typedef struct PPSourceMetrics { uint64_t raster_builds; } PPSourceMetrics;
/* Successful text/SVG raster generations and PNG/GIF cache decodes, accumulated
 * for this manager's lifetime. Cache hits, frame selection and failed requests
 * do not increment it. This observation does not allocate or retain content. */
PPSourceMetrics pp_assets_source_metrics(const PPAssets *assets);
/* Single-threaded manager; it must outlive every retained asset. Projects borrow
 * the app-owned manager. Imports copy the source bytes; IDs hash only those bytes.
 * Limits: 8192 per axis, 4096 GIF frames, 256 MiB decoded per resource, 512 MiB
 * total retained rasters, and 256 MiB source bytes per import.
 * Asset caches count RGBA + content_alpha automatically. Separately owned
 * text/presenter frames count only through explicit raster reservations below.
 * Source bytes, decoder scratch and SVG document scratch are outside it. */
bool pp_assets_create(PPAssets **out, PPError *error);
void pp_assets_destroy(PPAssets *assets);
bool pp_asset_import(PPAssets *assets, PPBytes bytes, PPAssetKind kind, PPAssetId *out, PPError *error);
PPAsset *pp_asset_retain(PPAssets *assets, PPAssetId id);
void pp_asset_release(PPAsset *asset);
/* Source remains valid until manager destruction, including cache eviction. */
PPBytes pp_asset_source(const PPAsset *asset);
PPAssetKind pp_asset_kind(const PPAsset *asset);
PPVec2 pp_asset_intrinsic_size(const PPAsset *asset);
/* Borrows the last available SVG raster without parsing, rendering, eviction
 * or metric changes. NULL means no decoded cache is available. Retain the asset
 * during use; a successful pp_asset_frame at a new size invalidates the borrow. */
const PPFrame *pp_asset_cached_svg(const PPAsset *asset);
/* Isolated SVG rendering from immutable bytes. No asset/cache/reservation or
 * GL state is touched. Validates the same supported SVG subset as import,
 * parses one private document and renders only the requested resolution.
 * out must be empty; caller owns RGBA + source alpha and any retained charge.
 * Bounds output before allocation and charges its overlap with the temporary
 * premultiplied ARGB surface against max_scratch_bytes. */
bool pp_svg_render_bounded(PPBytes source,int width,int height,
    size_t max_raster_bytes,size_t max_scratch_bytes,PPFrame *out,PPError *error);
/* Separately owned CPU rasters (text, scaled/presentation copies) must reserve
 * before retention and release exactly once at destruction. Includes stride *
 * height RGBA bytes + width * height coverage bytes. Reservations share the
 * 512 MiB budget with asset caches, including future cache allocations. Each
 * reservation is at most 256 MiB. GPU/source/transient decoder bytes excluded. */
bool pp_assets_reserve_raster(PPAssets *assets, size_t bytes, PPError *error);
void pp_assets_release_raster(PPAssets *assets, size_t bytes);
size_t pp_assets_external_raster_bytes(const PPAssets *assets);
size_t pp_assets_raster_bytes(const PPAssets *assets);
/* Evicts every unretained decoded cache, keeping owned source bytes. */
void pp_assets_trim_cache(PPAssets *assets);
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
