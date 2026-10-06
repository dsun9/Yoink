#ifndef POINTY_TEXT_RASTER_H
#define POINTY_TEXT_RASTER_H
#include "pointy/text.h"
#include <SDL3_ttf/SDL_ttf.h>
typedef struct PPTextRasterFont {
    TTF_Font *shaped_font;
    PPBytes bytes;
    int face_index;
} PPTextRasterFont;
/* Layout (0,0) in the physical raster, including destination hint overhang. */
bool pp_text_raster_origin(TTF_Text *text, const PPTextRasterFont fonts[2],
    double shaping_size, double requested_size, const uint8_t color[4],
    PPFrame *out, PPVec2 *origin_px, PPError *error);
bool pp_text_raster(TTF_Text *text, const PPTextRasterFont fonts[2],
    double shaping_size, double requested_size, const uint8_t color[4],
    PPFrame *out, PPError *error);
#endif
