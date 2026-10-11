#include "pointy/text.h"
#include "limits.h"
#include "text_raster.h"
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_ttf/SDL_textengine.h>
#include <math.h>
#include <limits.h>
#include <string.h>

typedef struct PPTextInspection {
    TTF_Font *fallback;
    PPTextDiagnostics *diagnostics;
} PPTextInspection;

static bool SDLCALL inspect_operations(void *userdata, TTF_Text *text) {
    PPTextInspection *inspection = userdata;
    bool previous_fallback = false;
    for (int i = 0; i < text->internal->num_ops; i++) {
        const TTF_DrawOperation *op = &text->internal->ops[i];
        if (op->cmd != TTF_DRAW_COMMAND_COPY) continue;
        bool fallback = op->copy.glyph_font == inspection->fallback;
        if (fallback && !previous_fallback) inspection->diagnostics->fallback_runs++;
        if (op->copy.glyph_index == 0) inspection->diagnostics->missing_glyphs++;
        previous_fallback = fallback;
    }
    text->internal->engine_text = inspection;
    return true;
}

static void SDLCALL destroy_inspection(void *userdata, TTF_Text *text) {
    (void)userdata;
    text->internal->engine_text = NULL;
}

static TTF_Font *open_memory(PPAsset *asset, int face, float size, SDL_IOStream **borrowed_io, PPError *error) {
    PPFontInfo info;
    PPBytes bytes = pp_asset_source(asset);
    if (asset->kind != PP_ASSET_FONT) {
        pp_media_error(error, PP_ERROR_ARGUMENT, "Text resource is not a font");
        return NULL;
    }
    if (!pp_font_inspect(bytes, face, &info, error)) return NULL;
    SDL_IOStream *io = SDL_IOFromConstMem(bytes.data, bytes.size);
    if (!io) {
        pp_media_error(error, PP_ERROR_MEMORY, SDL_GetError());
        return NULL;
    }
    TTF_Font *font = NULL;
    if (face == 0) {
        font = TTF_OpenFontIO(io, false, size);
    } else {
        SDL_PropertiesID props = SDL_CreateProperties();
        bool configured = props &&
            SDL_SetPointerProperty(props, TTF_PROP_FONT_CREATE_IOSTREAM_POINTER, io) &&
            SDL_SetFloatProperty(props, TTF_PROP_FONT_CREATE_SIZE_FLOAT, size) &&
            SDL_SetNumberProperty(props, TTF_PROP_FONT_CREATE_FACE_NUMBER, face);
        if (configured) font = TTF_OpenFontWithProperties(props);
        SDL_DestroyProperties(props);
    }
    if (!font) {
        pp_media_error(error, PP_ERROR_FORMAT, SDL_GetError());
        SDL_CloseIO(io);
        return NULL;
    }
    *borrowed_io = io;
    return font;
}

static bool valid_utf8(const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        unsigned char first = *p++;
        if (first < 0x80) continue;
        unsigned count;
        uint32_t code, minimum;
        if (first >= 0xc2 && first <= 0xdf) {
            count = 1;
            code = first & 31;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            count = 2;
            code = first & 15;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            count = 3;
            code = first & 7;
            minimum = 0x10000;
        } else {
            return false;
        }
        for (unsigned i = 0; i < count; i++) {
            if ((*p & 0xc0) != 0x80) return false;
            code = (code << 6) | (*p++ & 63);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}

static bool has_non_whitespace(const char *text) {
    /* Unicode White_Space: U+0009..000D, U+0020, U+0085, U+00A0,
     * U+1680, U+2000..200A, U+2028..2029, U+202F, U+205F, U+3000.
     * UTF-8 is validated before this policy is evaluated. */
    while (*text) {
        Uint32 code = SDL_StepUTF8(&text, NULL);
        bool whitespace = (code >= 0x9 && code <= 0xd) || code == 0x20 ||
            code == 0x85 || code == 0xa0 || code == 0x1680 ||
            (code >= 0x2000 && code <= 0x200a) ||
            (code >= 0x2028 && code <= 0x2029) ||
            code == 0x202f || code == 0x205f || code == 0x3000;
        if (!whitespace) return true;
    }
    return false;
}

bool pp_text_render(PPAssets *assets, const char *utf8, const PPTextStyle *style,
        double scale, PPFrame *out, PPTextDiagnostics *diagnostics, PPError *error) {
    PPTextDiagnostics local = {0};
    if (!diagnostics) diagnostics = &local;
    memset(diagnostics, 0, sizeof(*diagnostics));
    if (!assets || !utf8 || !style || !out || out->pixels || out->content_alpha ||
        !isfinite(scale) || scale <= 0 || !isfinite(style->font_size_dip) || style->font_size_dip <= 0)
        return pp_media_error(error, PP_ERROR_ARGUMENT, "Invalid text style, scale, or owned frame output");
    if (!*utf8 || !valid_utf8(utf8) || !has_non_whitespace(utf8))
        return pp_media_error(error, PP_ERROR_ARGUMENT, "Text must contain valid UTF-8 and a visible grab target");
    if (strlen(utf8) > INT_MAX)
        return pp_media_error(error, PP_ERROR_RESOURCE_LIMIT, "Text exceeds the shaper length limit");
    double size = style->font_size_dip * scale;
    if (!isfinite(size) || size > PP_MEDIA_MAX_DIMENSION)
        return pp_media_error(error, PP_ERROR_RESOURCE_LIMIT, "Text font size exceeds raster dimension limit");
    bool ok = false;
    bool initialized = false;
    PPAsset *primary_asset = pp_asset_retain(assets, style->primary_asset);
    PPAsset *fallback_asset = pp_asset_retain(assets, style->fallback_asset);
    TTF_Font *primary = NULL;
    TTF_Font *fallback = NULL;
    SDL_IOStream *primary_io = NULL;
    SDL_IOStream *fallback_io = NULL;
    TTF_Text *text = NULL;
    if (!primary_asset && !fallback_asset) {
        pp_media_error(error, PP_ERROR_ARGUMENT, "Import a primary font or emoji fallback font");
        goto done;
    }
    if (!TTF_Init()) {
        pp_media_error(error, PP_ERROR_RENDER, SDL_GetError());
        goto done;
    }
    initialized = true;
    /* SDL_ttf selects a bitmap strike instead of scaling it. Shape both fonts
     * at that strike's em size, then scale bitmap operations while rasterizing
     * outlines at destination size.
     * This preserves mixed advances, baselines, colors, and whole clusters. */
    double render_size = size;
    PPFontInfo info;
    if (fallback_asset) {
        if (!pp_font_inspect(pp_asset_source(fallback_asset), style->fallback_face, &info, error)) goto done;
        diagnostics->default_emoji_fallback = style->default_emoji_fallback &&
            !SDL_strcasecmp(info.family, "Noto Color Emoji");
        if (info.bitmap_em_size > 0) render_size = info.bitmap_em_size;
    }
    if (primary_asset) {
        if (!pp_font_inspect(pp_asset_source(primary_asset), style->primary_face, &info, error)) goto done;
        if (info.bitmap_em_size > 0) render_size = info.bitmap_em_size;
    }
    if (primary_asset) {
        primary = open_memory(primary_asset, style->primary_face, (float)render_size, &primary_io, error);
        if (!primary) goto done;
    }
    if (fallback_asset) {
        fallback = open_memory(fallback_asset, style->fallback_face, (float)render_size, &fallback_io, error);
        if (!fallback) goto done;
    }
    TTF_Font *font = primary ? primary : fallback;
    if (primary && fallback && !TTF_AddFallbackFont(primary, fallback)) {
        pp_media_error(error, PP_ERROR_RENDER, SDL_GetError());
        goto done;
    }
    if (style->requested_family && *style->requested_family &&
        (!primary || SDL_strcasecmp(style->requested_family, TTF_GetFontFamilyName(primary))))
        diagnostics->missing_requested_families = 1;
    /* Hand the full string to HarfBuzz/SDL_ttf: do not split clusters or ZWJ. */
    PPTextInspection inspection = {fallback, diagnostics};
    TTF_TextEngine engine;
    SDL_INIT_INTERFACE(&engine);
    engine.userdata = &inspection;
    engine.CreateText = inspect_operations;
    engine.DestroyText = destroy_inspection;
    text = TTF_CreateText(&engine, font, utf8, 0);
    int width = 0, height = 0;
    if (!text || !TTF_GetTextSize(text, &width, &height) || !TTF_UpdateText(text)) {
        pp_media_error(error, PP_ERROR_RENDER, SDL_GetError());
        goto done;
    }
    PPTextRasterFont fonts[2] = {
        {primary, pp_asset_source(primary_asset), style->primary_face},
        {fallback, pp_asset_source(fallback_asset), style->fallback_face}
    };
    if (!pp_text_raster(text, fonts, render_size, size, style->color, out, error)) goto done;
    size_t n = (size_t)out->width * out->height;
    for (size_t i = 0; i < n; i++) {
        if (out->content_alpha[i]) { ok = true; break; }
    }
    if (!ok) {
        pp_frame_destroy(out);
        pp_media_error(error, PP_ERROR_ARGUMENT, "Text needs visible glyphs to provide a draggable grab target");
    }
done:
    TTF_DestroyText(text);
    if (primary) TTF_ClearFallbackFonts(primary);
    TTF_CloseFont(primary);
    TTF_CloseFont(fallback);
    if (primary_io) SDL_CloseIO(primary_io);
    if (fallback_io) SDL_CloseIO(fallback_io);
    if (initialized) TTF_Quit();
    pp_asset_release(primary_asset);
    pp_asset_release(fallback_asset);
    if (ok) assets->metrics.raster_builds++;
    return ok;
}
