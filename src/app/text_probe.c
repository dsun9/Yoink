#include "pointy/text.h"
#include "pointy/platform.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

int pp_text_probe(unsigned duration_ms) {
    PPAssets *assets = NULL;
    PPSurface *surface = NULL;
    PPDisplay *displays = NULL;
    size_t display_count = 0;
    PPFrame frame = {0};
    PPError error = {0};
    PPTextStyle style = {0};
    int result = 1;
    if (!pp_assets_create(&assets, &error) ||
        !pp_text_default_style(assets, &style, &error)) goto done;
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
    PPAsset *emoji = pp_asset_retain(assets, style.fallback_asset);
    PPFontInfo info = {0};
    if (emoji) pp_font_inspect(pp_asset_source(emoji), style.fallback_face, &info, NULL);
    printf("Default hand: text=%s emoji=%s version=%s bundled fallback=%d fallback runs=%zu missing glyphs=%zu missing families=%zu; font=96 DIP scale=%.2f raster=%dx%d zero rotation full opacity\n",
        style.requested_family, info.family, info.version, diagnostics.default_emoji_fallback,
        diagnostics.fallback_runs, diagnostics.missing_glyphs, diagnostics.missing_requested_families,
        scale, frame.width, frame.height);
    pp_asset_release(emoji);
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
    pp_displays_free(displays);
    pp_surface_destroy(surface);
    pp_frame_destroy(&frame);
    pp_assets_destroy(assets);
    return result;
}
