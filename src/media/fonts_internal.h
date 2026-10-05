#ifndef POINTY_FONTS_INTERNAL_H
#define POINTY_FONTS_INTERNAL_H
#include "pointy/text.h"
/* Shared metadata-reader policy; synthetic tests exercise OS/2 fsType flags. */
void pp_font_embedding_flags(uint16_t flags, PPFontInfo *info);
/* Check shaped glyph indices even when .notdef has no drawable bitmap. */
bool pp_font_resolves_text(PPBytes bytes, int face_index, const char *utf8, PPError *error);
#endif
