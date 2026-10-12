#ifndef POINTY_TEXT_H
#define POINTY_TEXT_H
#include "pointy/assets.h"

typedef enum PPFontEmbedding {
    PP_FONT_INSTALLABLE, PP_FONT_EDITABLE, PP_FONT_PREVIEW_ONLY, PP_FONT_RESTRICTED
} PPFontEmbedding;
typedef struct PPFontInfo {
    char family[128], style[128];
    char version[128], copyright[512], license_url[256];
    int face_index;
    size_t face_count; /* Complete source collection, including unselected faces. */
    PPFontEmbedding embedding;
    bool embeddable, no_subsetting, bitmap_only; /* OS/2 embedding restrictions only. */
    bool scalable, bitmap_strikes, color_layers; /* Actual artwork capabilities. */
    unsigned colr_version; /* Table version, meaningful when color_layers is true. */
    size_t unicode_glyphs;
    bool covers_latin_a, covers_hand, color;
    double bitmap_em_size; /* Last fixed strike's em size, zero for scalable fonts. */
} PPFontInfo;
typedef struct PPTextStyle {
    const char *requested_family;
    PPAssetId primary_asset, fallback_asset;
    int primary_face, fallback_face;
    double font_size_dip;
    uint8_t color[4];
    bool default_emoji_fallback; /* This item selected bundled Noto when Segoe was unavailable. */
} PPTextStyle;
/* Artwork selected by the actual shaped draw operations, independent of font
 * embedding restrictions, fallback configuration, or glyph-image color type. */
typedef struct PPTextArtwork {
    size_t bitmap_glyphs, scalable_glyphs, unknown_glyphs;
    bool bitmap_only;
    double useful_pixels_per_dip;
} PPTextArtwork;
typedef struct PPTextDiagnostics {
    size_t fallback_runs, missing_glyphs, missing_requested_families;
    bool default_emoji_fallback; /* Selected Noto default, independent of requested text family. */
    PPTextArtwork artwork;
} PPTextDiagnostics;
/* A raster and its placement in the canonical item layout. The entire source
 * rectangle starts at layout_origin-raster_origin_px/raster_density and has
 * frame dimensions divided by raster_density. frame is owned; all metadata is
 * a value record. Canonical layout remains independent of disposable texels. */
typedef struct PPTextResult {
    PPFrame frame;
    PPTextArtwork artwork;
    PPVec2 layout_size, layout_origin, raster_origin_px;
    double raster_density;
} PPTextResult;
bool pp_font_inspect(PPBytes font, int face_index, PPFontInfo *out, PPError *error);
/* Copies installed Segoe UI Emoji and optional Aptos into owned assets. If
 * Segoe is unavailable, selects bundled Noto and records that choice. Ordinary
 * text-family selection remains independent. Never installs/bundles local fonts. */
bool pp_text_default_style(PPAssets *assets, PPTextStyle *out, PPError *error);
/* Single-threaded. Assets must outlive rendering. Fonts borrow retained source
 * bytes until closed, including fallback fonts. out must be empty and becomes
 * an owned premultiplied frame (pp_frame_destroy). Diagnostics are value records.
 * Fallback runs count contiguous drawn runs from the actual shaping operations;
 * missing_glyphs counts drawn glyph-index-zero operations. Empty/invisible text
 * fails validation. Logical raster bounds are width/scale and height/scale.
 * Bounds include native glyph overhangs; all glyphs share an origin translation
 * when destination coverage extends left/above the measured layout.
 * Retained CPU bytes are stride*height + width*height (RGBA + content_alpha).
 * Per-frame limits apply; caller budgets retained text + presentation rasters
 * together with asset caches. This owned frame is outside the asset cache. */
bool pp_text_render(PPAssets *assets, const char *utf8, const PPTextStyle *style,
    double pixels_per_dip, PPFrame *out, PPTextDiagnostics *diagnostics, PPError *error);
bool pp_text_render_result(PPAssets *assets,const char *utf8,const PPTextStyle *style,
    double pixels_per_dip,PPTextResult *out,PPTextDiagnostics *diagnostics,PPError *error);
/* Same fixed canonical shaping, with an allocation limit checked before the
 * final RGBA/coverage raster. Used for owner-reserved asynchronous candidates. */
bool pp_text_render_result_bounded(PPAssets *assets,const char *utf8,const PPTextStyle *style,
    double pixels_per_dip,size_t max_raster_bytes,PPTextResult *out,PPTextDiagnostics *diagnostics,PPError *error);
#endif
