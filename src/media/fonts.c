#include "fonts_internal.h"
#include "limits.h"
#include "pointy/platform.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void pp_blob_destroy(PPBlob *blob) {
    if (!blob) return;
    free(blob->data);
    memset(blob, 0, sizeof(*blob));
}

void pp_font_embedding_flags(uint16_t flags, PPFontInfo *info) {
    info->embedding = PP_FONT_INSTALLABLE;
    if (flags & 2) info->embedding = PP_FONT_RESTRICTED;
    else if (flags & 4) info->embedding = PP_FONT_PREVIEW_ONLY;
    else if (flags & 8) info->embedding = PP_FONT_EDITABLE;
    info->no_subsetting = (flags & 0x100) != 0;
    info->bitmap_only = (flags & 0x200) != 0;
    /* Projects retain complete outline font bytes, including no-subsetting
     * fonts. Preview/restricted/bitmap-only embedding needs another resource. */
    info->embeddable = !info->bitmap_only &&
        (info->embedding == PP_FONT_INSTALLABLE || info->embedding == PP_FONT_EDITABLE);
}

bool pp_font_inspect(PPBytes bytes, int face_index, PPFontInfo *out, PPError *error) {
    if (out) memset(out, 0, sizeof(*out));
    if (!out || !bytes.data || !bytes.size || bytes.size > LONG_MAX || face_index < 0)
        return pp_media_error(error, PP_ERROR_ARGUMENT, "Invalid font bytes or face index");
    FT_Library library = NULL;
    FT_Face face = NULL;
    if (FT_Init_FreeType(&library))
        return pp_media_error(error, PP_ERROR_MEMORY, "Could not initialize font inspection");
    bool ok = false;
    if (FT_New_Memory_Face(library, bytes.data, (FT_Long)bytes.size, face_index, &face)) {
        pp_media_error(error, PP_ERROR_FORMAT, "Corrupt font or invalid collection face index");
        goto done;
    }
    out->face_index = face_index;
    snprintf(out->family, sizeof(out->family), "%s", face->family_name ? face->family_name : "Unknown");
    snprintf(out->style, sizeof(out->style), "%s", face->style_name ? face->style_name : "Unknown");
    pp_font_embedding_flags(FT_Get_FSType_Flags(face), out);
    out->color = FT_HAS_COLOR(face) != 0;
    if (!FT_IS_SCALABLE(face) && face->num_fixed_sizes > 0)
        out->bitmap_em_size = face->available_sizes[face->num_fixed_sizes - 1].y_ppem / 64.0;
    if (FT_Select_Charmap(face, FT_ENCODING_UNICODE)) {
        pp_media_error(error, PP_ERROR_FORMAT, "Font has no Unicode character map");
        goto done;
    }
    out->covers_latin_a = FT_Get_Char_Index(face, 'A') != 0;
    out->covers_hand = FT_Get_Char_Index(face, 0x1f449) != 0;
    FT_UInt glyph;
    FT_ULong code = FT_Get_First_Char(face, &glyph);
    while (glyph) {
        out->unicode_glyphs++;
        code = FT_Get_Next_Char(face, code, &glyph);
    }
    ok = true;
done:
    if (face) FT_Done_Face(face);
    FT_Done_FreeType(library);
    return ok;
}
