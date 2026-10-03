#include "pointy/text.h"
#include "pointy/platform.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

static bool import_file(PPAssets *assets, const char *path, PPAssetId *id, PPError *error) {
    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    if (!data) {
        snprintf(error->message, sizeof(error->message), "%s", SDL_GetError());
        return false;
    }
    bool ok = pp_asset_import(assets, (PPBytes){data, size}, PP_ASSET_FONT, id, error);
    SDL_free(data);
    return ok;
}

int pp_text_probe(unsigned duration_ms) {
    PPAssets *assets = NULL;
    PPSurface *surface = NULL;
    PPDisplay *displays = NULL;
    size_t display_count = 0;
    PPFrame frame = {0};
    PPError error = {0};
    PPBlob aptos = {0};
    PPTextStyle style = {0};
    style.requested_family = "Aptos";
    style.font_size_dip = 96;
    style.color[3] = 255;
    int result = 1;
    if (!pp_assets_create(&assets, &error) ||
        !import_file(assets, PP_RESOURCES "/fonts/NotoColorEmoji.ttf", &style.fallback_asset, &error)) goto done;
    if (pp_platform_find_font("Aptos", &aptos, NULL)) {
        if (!pp_asset_import(assets, (PPBytes){aptos.data, aptos.size}, PP_ASSET_FONT,
                &style.primary_asset, &error)) goto done;
        pp_blob_destroy(&aptos);
    }
    if (!pp_platform_displays(&displays, &display_count, &error) || !display_count) goto done;
    double scale = displays[0].drawable_pixels_per_dip;
    PPTextDiagnostics diagnostics;
    if (!pp_text_render(assets, "\xf0\x9f\x91\x89", &style, scale, &frame, &diagnostics, &error) ||
        !pp_surface_create(&surface, &error) ||
        !pp_surface_present(surface, &frame, (PPRect){220,220,frame.width,frame.height}, &error)) goto done;
    SDL_Surface *image = SDL_CreateSurfaceFrom(frame.width, frame.height,
        SDL_PIXELFORMAT_RGBA32, frame.pixels, (int)frame.stride);
    if (!image || !SDL_SaveBMP(image, PP_PROBE_IMAGE)) {
        snprintf(error.message, sizeof(error.message), "%s", SDL_GetError());
        SDL_DestroySurface(image);
        goto done;
    }
    SDL_DestroySurface(image);
    printf("Default hand: Aptos requested, Noto fallback runs=%zu missing glyphs=%zu missing families=%zu; font=96 DIP scale=%.2f raster=%dx%d zero rotation full opacity\n",
        diagnostics.fallback_runs, diagnostics.missing_glyphs, diagnostics.missing_requested_families,
        scale, frame.width, frame.height);
    Uint64 start = SDL_GetTicks();
    while (SDL_GetTicks() - start < (duration_ms ? duration_ms : 1500)) {
        PPPlatformEvent event;
        while (pp_platform_poll(&event)) {
            if (event.kind == PP_EVENT_QUIT) goto done;
        }
        SDL_Delay(1);
    }
    result = 0;
done:
    if (result) fprintf(stderr, "Default hand probe failed: %s\n", error.message);
    pp_blob_destroy(&aptos);
    pp_displays_free(displays);
    pp_surface_destroy(surface);
    pp_frame_destroy(&frame);
    pp_assets_destroy(assets);
    return result;
}
