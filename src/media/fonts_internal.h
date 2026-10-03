#ifndef POINTY_FONTS_INTERNAL_H
#define POINTY_FONTS_INTERNAL_H
#include "pointy/text.h"
/* Shared metadata-reader policy; synthetic tests exercise OS/2 fsType flags. */
void pp_font_embedding_flags(uint16_t flags, PPFontInfo *info);
#endif
