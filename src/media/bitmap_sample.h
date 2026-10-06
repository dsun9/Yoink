#ifndef POINTY_BITMAP_SAMPLE_H
#define POINTY_BITMAP_SAMPLE_H
#include "pointy/render.h"
typedef struct PPBitmapRect { int x,y,w,h; } PPBitmapRect;
typedef struct PPBitmapSampling {
    PPBitmapRect source, destination;
    bool alpha_only;
} PPBitmapSampling;
/* Internal bounded CPU kernel. Caller validates glyph/output storage and
 * destination extents through pp_media_size before entering this hot loop.
 * Integer crops, source-pixel centers, premultiplied over and rounding match
 * the text rasterizer. All scratch is transient; no retained/global cache. */
bool pp_bitmap_sample(PPFrame *out, const PPFrame *glyph, const PPBitmapSampling *sample,
                      double scale, int origin_x, int origin_y,
                      const uint8_t color[4], PPError *error);
#endif
