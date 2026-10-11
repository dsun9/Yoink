#include "fonts_internal.h"
#include "limits.h"
#include "pointy/platform.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_COLOR_H
#include FT_TRUETYPE_TABLES_H
#include <hb.h>
#include <hb-ot.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void pp_blob_destroy(PPBlob *blob) {
    if (!blob) return;
    free(blob->data);
    memset(blob, 0, sizeof(*blob));
}

bool pp_font_resolves_text(PPBytes bytes, int face_index, const char *utf8, PPError *error) {
    if (!bytes.data || !bytes.size || bytes.size > UINT_MAX || face_index < 0 || !utf8)
        return pp_media_error(error, PP_ERROR_ARGUMENT, "Invalid font coverage input");
    hb_blob_t *blob = hb_blob_create((const char *)bytes.data, (unsigned)bytes.size,
                                    HB_MEMORY_MODE_READONLY, NULL, NULL);
    hb_face_t *face = hb_face_create(blob, (unsigned)face_index);
    hb_font_t *font = hb_font_create(face);
    hb_ot_font_set_funcs(font);
    hb_buffer_t *buffer = hb_buffer_create();
    bool ok = hb_blob_get_length(blob) != 0 && hb_face_get_glyph_count(face) != 0 &&
              font != hb_font_get_empty() && hb_buffer_allocation_successful(buffer);
    /* Match layout boundaries; shape whole runs so ZWJ/variation-selector
     * sequences resolve as clusters rather than requiring standalone glyphs. */
    for (const char *run = utf8; ok && *run;) {
        size_t length = strcspn(run, "\r\n\t");
        if (length > INT_MAX) { ok = false; break; }
        if (length) {
            hb_buffer_clear_contents(buffer);
            hb_buffer_set_flags(buffer, HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES);
            hb_buffer_add_utf8(buffer, run, (int)length, 0, (int)length);
            hb_buffer_guess_segment_properties(buffer);
            hb_shape(font, buffer, NULL, 0);
            unsigned count;
            const hb_glyph_info_t *glyphs = hb_buffer_get_glyph_infos(buffer, &count);
            ok = hb_buffer_allocation_successful(buffer);
            for (unsigned i = 0; ok && i < count; i++) ok = glyphs[i].codepoint != 0;
        }
        run += length;
        if (*run) run++;
    }
    hb_buffer_destroy(buffer);
    hb_font_destroy(font);
    hb_face_destroy(face);
    hb_blob_destroy(blob);
    return ok || pp_media_error(error, PP_ERROR_FORMAT,
                               "Text contains unresolved glyphs in the embedded fallback");
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

static void font_names(PPBytes bytes, int index, PPFontInfo *info) {
    hb_blob_t *blob = hb_blob_create((const char *)bytes.data, (unsigned)bytes.size,
                                    HB_MEMORY_MODE_READONLY, NULL, NULL);
    hb_face_t *face = hb_face_create(blob, (unsigned)index);
    struct FontName { hb_ot_name_id_t id; char *text; unsigned size; } names[] = {
        {HB_OT_NAME_ID_VERSION_STRING, info->version, sizeof(info->version)},
        {HB_OT_NAME_ID_COPYRIGHT, info->copyright, sizeof(info->copyright)},
        {HB_OT_NAME_ID_LICENSE_URL, info->license_url, sizeof(info->license_url)}
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        unsigned length = names[i].size;
        hb_ot_name_get_utf8(face, names[i].id, hb_language_from_string("en", -1),
                           &length, names[i].text);
        names[i].text[names[i].size - 1] = 0;
    }
    hb_face_destroy(face);
    hb_blob_destroy(blob);
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
    out->face_count = (size_t)face->num_faces;
    snprintf(out->family, sizeof(out->family), "%s", face->family_name ? face->family_name : "Unknown");
    snprintf(out->style, sizeof(out->style), "%s", face->style_name ? face->style_name : "Unknown");
    font_names(bytes, face_index, out);
    pp_font_embedding_flags(FT_Get_FSType_Flags(face), out);
    out->color = FT_HAS_COLOR(face) != 0;
    out->scalable = FT_IS_SCALABLE(face) != 0;
    out->bitmap_strikes = face->num_fixed_sizes > 0;
    FT_Byte colr[2];
    FT_ULong colr_length = sizeof(colr);
    if (!FT_Load_Sfnt_Table(face, FT_MAKE_TAG('C', 'O', 'L', 'R'), 0, colr, &colr_length) &&
        colr_length == sizeof(colr)) {
        out->colr_version = ((unsigned)colr[0] << 8) | colr[1];
        /* Include unencoded glyphs that shaping can select. A COLRv1 table
         * alone does not imply that the current rasterizer has v0 layers. */
        for (FT_UInt glyph = 0; !out->color_layers && glyph < (FT_ULong)face->num_glyphs; glyph++) {
            FT_LayerIterator iterator = {0};
            FT_UInt layer, color;
            out->color_layers = FT_Get_Color_Glyph_Layer(face, glyph, &layer, &color, &iterator) != 0;
        }
    }
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
