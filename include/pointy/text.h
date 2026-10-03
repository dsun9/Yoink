#ifndef POINTY_TEXT_H
#define POINTY_TEXT_H
#include "pointy/assets.h"

typedef enum PPFontEmbedding {
    PP_FONT_INSTALLABLE, PP_FONT_EDITABLE, PP_FONT_PREVIEW_ONLY, PP_FONT_RESTRICTED
} PPFontEmbedding;
typedef struct PPFontInfo {
    char family[128], style[128];
    int face_index;
    size_t face_count; /* Complete source collection, including unselected faces. */
    PPFontEmbedding embedding;
    bool embeddable, no_subsetting, bitmap_only;
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
} PPTextStyle;
typedef struct PPTextDiagnostics {
    size_t fallback_runs, missing_glyphs, missing_requested_families;
} PPTextDiagnostics;
bool pp_font_inspect(PPBytes font, int face_index, PPFontInfo *out, PPError *error);
/* Loads owned bundled fallback and optional local Aptos bytes into assets. */
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
#endif
