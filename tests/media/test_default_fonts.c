#include "pointy/model.h"
#include "pointy/platform.h"
#include "pointy/project_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PPError error;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error.message); exit(1); } } while (0)
static bool installed = true;
static bool missing_text = false;

/* Replace only the installed-font system boundary. These are original OFL
 * glyphs with synthetic family metadata, never Windows font bytes. */
bool pp_test_find_font(const char *family, PPBlob *out, PPError *lookup_error) {
    (void)lookup_error;
    if (!installed || (strcmp(family, "Aptos") && strcmp(family, "Segoe UI Emoji"))) return false;
    if (missing_text && !strcmp(family, "Aptos")) return false;
    FILE *file = fopen(PP_FONT_FIXTURES "/PointyDefaultFonts.ttc", "rb");
    CHECK(file && !fseek(file, 0, SEEK_END));
    long length = ftell(file);
    CHECK(length > 0);
    rewind(file);
    *out = (PPBlob){malloc((size_t)length), (size_t)length};
    CHECK(out->data && fread(out->data, 1, out->size, file) == out->size);
    fclose(file);
    return true;
}

static void test_installed_emoji_selection(void) {
    PPAssets *assets = NULL;
    PPTextStyle style = {0};
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_text_default_style(assets, &style, &error));
    PPAsset *emoji = pp_asset_retain(assets, style.fallback_asset);
    PPFontInfo info;
    CHECK(emoji && pp_font_inspect(pp_asset_source(emoji), style.fallback_face, &info, &error));
    CHECK(!strcmp(info.family, "Segoe UI Emoji") && style.fallback_face == 1);
    CHECK(!style.default_emoji_fallback);
    PPAsset *text = pp_asset_retain(assets, style.primary_asset);
    CHECK(text && pp_font_inspect(pp_asset_source(text), style.primary_face, &info, &error));
    CHECK(!strcmp(info.family, "Aptos") && style.primary_face == 0);
    PPFrame frame = {0};
    PPTextDiagnostics diagnostics;
    CHECK(pp_text_render(assets, "A\xf0\x9f\x91\x89", &style, 1, &frame, &diagnostics, &error));
    CHECK(!diagnostics.missing_glyphs && !diagnostics.missing_requested_families && diagnostics.fallback_runs == 1);
    pp_frame_destroy(&frame);
    pp_asset_release(text);
    pp_asset_release(emoji);
    pp_assets_destroy(assets);
}

static void test_unavailable_emoji_diagnostic(void) {
    installed = false;
    PPAssets *assets = NULL;
    PPTextStyle style = {0};
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_text_default_style(assets, &style, &error));
    CHECK(style.default_emoji_fallback && !strcmp(style.requested_family, "Aptos"));
    PPAsset *emoji = pp_asset_retain(assets, style.fallback_asset);
    PPFontInfo info;
    CHECK(emoji && pp_font_inspect(pp_asset_source(emoji), style.fallback_face, &info, &error));
    CHECK(!strcmp(info.family, "Noto Color Emoji"));
    PPFrame frame = {0};
    PPTextDiagnostics diagnostics;
    CHECK(pp_text_render(assets, "\xf0\x9f\x91\x89", &style, 1, &frame, &diagnostics, &error));
    CHECK(diagnostics.default_emoji_fallback && diagnostics.missing_requested_families == 1 && !diagnostics.missing_glyphs);
    pp_frame_destroy(&frame);
    pp_asset_release(emoji);
    pp_assets_destroy(assets);
    installed = true;
}

static void test_artwork_metadata(void) {
    PPBlob bytes = {0};
    CHECK(pp_test_find_font("Segoe UI Emoji", &bytes, NULL));
    PPFontInfo info;
    CHECK(pp_font_inspect((PPBytes){bytes.data, bytes.size}, 1, &info, &error));
    CHECK(info.scalable && info.color_layers && !info.bitmap_strikes && info.colr_version == 0);
    CHECK(info.embeddable && !info.bitmap_only && info.embedding == PP_FONT_EDITABLE);
    CHECK(strstr(info.version, "7.25") && strstr(info.copyright, "PointyPointer contributors"));
    CHECK(pp_font_inspect((PPBytes){bytes.data, bytes.size}, 0, &info, &error));
    CHECK(info.color_layers && !info.covers_hand); /* Unencoded shaped alternates can have layers. */
    pp_blob_destroy(&bytes);
    installed = false;
    PPAssets *assets = NULL;
    PPTextStyle style = {0};
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_text_default_style(assets, &style, &error));
    PPAsset *noto = pp_asset_retain(assets, style.fallback_asset);
    CHECK(noto && pp_font_inspect(pp_asset_source(noto), 0, &info, &error));
    CHECK(!info.scalable && info.bitmap_strikes && !info.color_layers && info.color);
    CHECK(info.embeddable && !info.bitmap_only); /* Bitmap artwork is not an embedding restriction. */
    pp_asset_release(noto);
    pp_assets_destroy(assets);
    installed = true;
}

static void test_selected_font_notice(void) {
    PPAssets *assets = NULL;
    PPProject *project = NULL;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_default(assets, &project, &error));
    const PPItem *item = pp_project_target(project, PP_ANCHOR_TOP_LEFT);
    const char *notice = pp_project_notice(project, item->text_style.fallback_asset);
    CHECK(strstr(notice, "Segoe UI Emoji") && strstr(notice, "PointyPointer contributors") && !strstr(notice, "Noto"));
    pp_project_destroy(project);
    pp_assets_destroy(assets);
}

static void test_portable_installed_emoji_without_text_family(void) {
    missing_text = true;
    PPAssets *assets = NULL;
    PPProject *project = NULL;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_default(assets, &project, &error));
    const PPItem *item = pp_project_target(project, PP_ANCHOR_TOP_LEFT);
    PPFrame original = {0};
    PPTextDiagnostics diagnostics;
    CHECK(pp_text_render(assets, item->text, &item->text_style, 1, &original, &diagnostics, &error));
    CHECK(diagnostics.missing_requested_families == 1 && !diagnostics.missing_glyphs);
    CHECK(pp_project_save(project, PP_FONT_OUTPUT "/installed-font.pointy", &error));
    pp_project_destroy(project);
    pp_assets_destroy(assets);
    installed = false; /* Reopen cannot obtain the font from its original lookup. */
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_load(PP_FONT_OUTPUT "/installed-font.pointy", assets, &project, &error));
    item = pp_project_target(project, PP_ANCHOR_TOP_LEFT);
    PPFrame reopened = {0};
    CHECK(pp_text_render(assets, item->text, &item->text_style, 1, &reopened, &diagnostics, &error));
    CHECK(item->text_style.fallback_face == 1 && !item->text_style.default_emoji_fallback);
    CHECK(original.width == reopened.width && original.height == reopened.height && original.stride == reopened.stride);
    CHECK(!memcmp(original.pixels, reopened.pixels, original.stride * original.height));
    CHECK(strstr(pp_project_notice(project, item->text_style.fallback_asset), "PointyPointer contributors"));
    pp_frame_destroy(&reopened);
    pp_frame_destroy(&original);
    pp_project_destroy(project);
    pp_assets_destroy(assets);
    CHECK(pp_platform_remove_file(PP_FONT_OUTPUT "/installed-font.pointy", &error));
    installed = true;
    missing_text = false;
}

static void test_portable_noto_keeps_fallback_diagnostic(void) {
    installed = false;
    PPAssets *assets = NULL;
    PPProject *project = NULL;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_default(assets, &project, &error));
    CHECK(pp_project_save(project, PP_FONT_OUTPUT "/noto-font.pointy", &error));
    pp_project_destroy(project);
    pp_assets_destroy(assets);
    installed = true;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_load(PP_FONT_OUTPUT "/noto-font.pointy", assets, &project, &error));
    const PPItem *item = pp_project_target(project, PP_ANCHOR_TOP_LEFT);
    CHECK(item->text_style.default_emoji_fallback);
    PPFrame frame = {0};
    PPTextDiagnostics diagnostics;
    CHECK(pp_text_render(assets, item->text, &item->text_style, 1, &frame, &diagnostics, &error));
    CHECK(diagnostics.default_emoji_fallback);
    CHECK(strstr(pp_project_notice(project, item->text_style.fallback_asset), "SIL OPEN FONT LICENSE"));
    pp_frame_destroy(&frame);
    pp_project_destroy(project);
    pp_assets_destroy(assets);
    CHECK(pp_platform_remove_file(PP_FONT_OUTPUT "/noto-font.pointy", &error));
}

int main(void) {
    test_installed_emoji_selection();
    test_unavailable_emoji_diagnostic();
    test_artwork_metadata();
    test_selected_font_notice();
    test_portable_installed_emoji_without_text_family();
    test_portable_noto_keeps_fallback_diagnostic();
    puts("installed default emoji selection passed");
    return 0;
}
