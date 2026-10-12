#include "pointy/text.h"
#include "pointy/platform.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include "resources.h"

void *pp_resource_load(const char *relative, size_t *size) {
    const char *base=SDL_GetBasePath();
    if(!base) return NULL;
    char *path=NULL;
    if(SDL_asprintf(&path,"%sresources/%s",base,relative)<0) return NULL;
    void *data=SDL_LoadFile(path,size);
    SDL_free(path);
    return data;
}

/* Lookup returns complete privately owned files. Select the matching collection
 * face from metadata instead of assuming that the first face is the family. */
static bool import_local(PPAssets *assets, const char *family, PPAssetId *asset,
                         int *selected_face, bool *found, PPError *error) {
    *found = false;
#if defined(_WIN32) || defined(__APPLE__)
    PPBlob local = {0};
    if (!pp_platform_find_font(family, &local, NULL)) return true;
    PPBytes bytes = {local.data, local.size};
    PPFontInfo info;
    bool ok = true;
    if (pp_font_inspect(bytes, 0, &info, NULL) && info.face_count <= 4096) {
        size_t count = info.face_count;
        for (size_t face = 0; face < count; face++) {
            if (!pp_font_inspect(bytes, (int)face, &info, NULL)) break;
            if (SDL_strcasecmp(info.family, family)) continue;
            ok = pp_asset_import(assets, bytes, PP_ASSET_FONT, asset, error);
            if (ok) { *selected_face = (int)face; *found = true; }
            break;
        }
    }
    pp_blob_destroy(&local);
    return ok;
#else
    (void)assets; (void)family; (void)asset; (void)selected_face; (void)error;
    return true;
#endif
}

/* Local font lookup remains separate from redistributable bundled resources. */
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
    bool found = false;
    if (!import_local(assets, "Segoe UI Emoji", &style.fallback_asset,
                      &style.fallback_face, &found, error)) return false;
    if (!found) {
        size_t length = 0;
        void *data = pp_resource_load("fonts/NotoColorEmoji.ttf", &length);
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
        style.default_emoji_fallback = true;
    }
    if (!import_local(assets, "Aptos", &style.primary_asset,
                      &style.primary_face, &found, error)) return false;
    *out = style;
    return true;
}
