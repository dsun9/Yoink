#include "pointy/text.h"
#include "pointy/platform.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

/* Development resource discovery; the packaging task supplies a relocatable
 * bundled path here. Local font lookup stays behind this media/platform edge. */
bool pp_text_default_style(PPAssets *assets, PPTextStyle *out, PPError *error) {
    if (!assets || !out) {
        if (error) {
            error->code = PP_ERROR_ARGUMENT;
            snprintf(error->message, sizeof(error->message), "Missing default font assets or output");
        }
        return false;
    }
    PPTextStyle style = {0};
    style.requested_family = "Aptos";
    style.font_size_dip = 96.0;
    style.color[3] = 255;
    size_t length = 0;
    void *data = SDL_LoadFile(PP_RESOURCES "/fonts/NotoColorEmoji.ttf", &length);
    if (!data) {
        if (error) {
            error->code = PP_ERROR_PLATFORM;
            snprintf(error->message, sizeof(error->message), "Cannot load bundled Noto Color Emoji: %s", SDL_GetError());
        }
        return false;
    }
    bool ok = pp_asset_import(assets, (PPBytes){data, length}, PP_ASSET_FONT, &style.fallback_asset, error);
    SDL_free(data);
    if (!ok) return false;
#ifdef _WIN32
    PPBlob local = {0};
    if (pp_platform_find_font("Aptos", &local, NULL)) {
        /* Preserve the actual matching collection face, not just its bytes. */
        for (int face = 0; face < 256; face++) {
            PPFontInfo info;
            if (!pp_font_inspect((PPBytes){local.data, local.size}, face, &info, NULL)) break;
            if (!SDL_strcasecmp(info.family, "Aptos")) {
                ok = pp_asset_import(assets, (PPBytes){local.data, local.size}, PP_ASSET_FONT, &style.primary_asset, error);
                style.primary_face = face;
                break;
            }
        }
        pp_blob_destroy(&local);
        if (!ok) return false;
    }
#endif
    *out = style;
    return true;
}
