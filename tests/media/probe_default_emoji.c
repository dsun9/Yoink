#include "pointy/project_store.h"
#include "pointy/platform.h"
#include <SDL3/SDL.h>
#include <hb.h>
#include <hb-ot.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PPError error;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error.message); exit(1); } } while (0)

static void report_regional_indicators(PPBytes bytes, int index) {
    hb_blob_t *blob = hb_blob_create((const char *)bytes.data, (unsigned)bytes.size,
                                    HB_MEMORY_MODE_READONLY, NULL, NULL);
    hb_face_t *face = hb_face_create(blob, (unsigned)index);
    hb_font_t *font = hb_font_create(face);
    hb_ot_font_set_funcs(font);
    hb_buffer_t *buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8", -1, 0, -1);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font, buffer, NULL, 0);
    unsigned count = 0;
    const hb_glyph_info_t *glyphs = hb_buffer_get_glyph_infos(buffer, &count);
    CHECK(count > 0);
    printf("Regional-indicator US sequence: %u shaped glyphs", count);
    for (unsigned i = 0; i < count; i++) { CHECK(glyphs[i].codepoint); printf(" %u", glyphs[i].codepoint); }
    puts(". This records font coverage; no per-sequence fallback policy is applied.");
    hb_buffer_destroy(buffer);
    hb_font_destroy(font);
    hb_face_destroy(face);
    hb_blob_destroy(blob);
}

int main(int argc, char **argv) {
    PPBlob installed = {0};
    if (!pp_platform_find_font("Segoe UI Emoji", &installed, NULL)) {
        puts("Installed Segoe UI Emoji unavailable; production font check skipped.");
        return argc > 1 && !strcmp(argv[1], "--require-installed") ? 1 : 77;
    }
    pp_blob_destroy(&installed);
    PPAssets *assets = NULL;
    PPProject *project = NULL;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_default(assets, &project, &error));
    const PPItem *item = pp_project_target(project, PP_ANCHOR_TOP_LEFT);
    PPTextStyle style = item->text_style;
    PPAsset *font = pp_asset_retain(assets, style.fallback_asset);
    PPFontInfo info;
    CHECK(font && pp_font_inspect(pp_asset_source(font), style.fallback_face, &info, &error));
    CHECK(!strcmp(info.family, "Segoe UI Emoji") && !style.default_emoji_fallback);
    printf("Selected emoji: family=%s version=%s face=%d/%zu scalable=%d bitmap-strikes=%d color-layers=%d COLR-version=%u embedding=%d embeddable=%d\n",
        info.family, info.version, info.face_index, info.face_count, info.scalable,
        info.bitmap_strikes, info.color_layers, info.colr_version, info.embedding, info.embeddable);
    report_regional_indicators(pp_asset_source(font), style.fallback_face);
    pp_asset_release(font);
    struct Case { const char *name, *text; bool color; } cases[] = {
        {"hand", "\xf0\x9f\x91\x89", true},
        {"skin-tone", "\xf0\x9f\x91\x89\xf0\x9f\x8f\xbd", true},
        {"heart-VS16", "\xe2\x9d\xa4\xef\xb8\x8f", true},
        {"joined", "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb", true},
        {"keycap", "1\xef\xb8\x8f\xe2\x83\xa3", true},
        {"mixed", "A\xf0\x9f\x91\x89\nA", true},
        {"regional-indicators", "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8", false},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        PPFrame frame = {0};
        PPTextDiagnostics diagnostics;
        CHECK(pp_text_render(assets, cases[i].text, &style, 1.5, &frame, &diagnostics, &error));
        CHECK(!diagnostics.missing_glyphs && !diagnostics.default_emoji_fallback);
        size_t visible = 0, colored = 0;
        for (size_t pixel = 0; pixel < (size_t)frame.width * frame.height; pixel++) {
            const uint8_t *p = frame.pixels + pixel * 4;
            visible += p[3] > 0;
            colored += p[3] > 0 && (p[0] != p[1] || p[1] != p[2]);
            CHECK(p[0] <= p[3] && p[1] <= p[3] && p[2] <= p[3]);
        }
        CHECK(visible && (!cases[i].color || colored));
        printf("%s: raster=%dx%d colored=%zu visible=%zu fallback-runs=%zu missing=%zu missing-text-family=%zu\n",
            cases[i].name, frame.width, frame.height, colored, visible,
            diagnostics.fallback_runs, diagnostics.missing_glyphs, diagnostics.missing_requested_families);
        char path[1024];
        snprintf(path, sizeof(path), "%s/default-emoji-%s.bmp", PP_FONT_OUTPUT, cases[i].name);
        SDL_Surface *surface = SDL_CreateSurfaceFrom(frame.width, frame.height, SDL_PIXELFORMAT_RGBA32, frame.pixels, (int)frame.stride);
        CHECK(surface && SDL_SaveBMP(surface, path));
        SDL_DestroySurface(surface);
        pp_frame_destroy(&frame);
    }
    CHECK(info.embeddable);
    CHECK(pp_project_save(project, PP_FONT_OUTPUT "/default-emoji.pointy", &error));
    PPFrame original = {0};
    CHECK(pp_text_render(assets, item->text, &style, 1.5, &original, NULL, &error));
    pp_project_destroy(project);
    pp_assets_destroy(assets);
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_load(PP_FONT_OUTPUT "/default-emoji.pointy", assets, &project, &error));
    item = pp_project_target(project, PP_ANCHOR_TOP_LEFT);
    PPFrame reopened = {0};
    CHECK(pp_text_render(assets, item->text, &item->text_style, 1.5, &reopened, NULL, &error));
    CHECK(original.width == reopened.width && original.height == reopened.height && original.stride == reopened.stride);
    CHECK(!memcmp(original.pixels, reopened.pixels, original.stride * original.height));
    CHECK(strstr(pp_project_notice(project, item->text_style.fallback_asset), "Segoe UI Emoji"));
    puts("Production default hand reopens identically from privately embedded bytes.");
    pp_frame_destroy(&original);
    pp_frame_destroy(&reopened);
    pp_project_destroy(project);
    pp_assets_destroy(assets);
    CHECK(pp_platform_remove_file(PP_FONT_OUTPUT "/default-emoji.pointy", &error));
    return 0;
}
