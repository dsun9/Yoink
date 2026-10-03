#include "pointy/model.h"
#include "../test.h"
#include <math.h>
#include <string.h>

static void test_project_defaults(void) {
    PPAssets *assets = NULL;
    PPProject *project = NULL;
    PPError error = {0};
    CHECK(pp_assets_create(&assets, &error));
    CHECK(!pp_text_default_style(assets, NULL, &error));
    CHECK(pp_project_default(assets, &project, &error));
    CHECK(pp_project_validate(project, &error));
    CHECK(PP_ANCHOR_COUNT == 8);
    const PPProjectSettings *settings = pp_project_settings(project);
    CHECK(settings->activation_dip == 16.0);
    CHECK(settings->rotation_step_deg == 10.0);
    CHECK(settings->resize_factor == 1.1);
    CHECK(settings->easing_seconds == .12);
    for (int a = 0; a < PP_ANCHOR_COUNT; a++) {
        const PPAnchorList *list = pp_project_anchor(project, (PPAnchor)a);
        CHECK(list && list->enabled && list->count == 1);
        const PPItem *item = pp_project_target(project, (PPAnchor)a);
        CHECK(item && item->id == list->default_id && item->kind == PP_ITEM_TEXT);
        CHECK(!strcmp(item->text, "\xf0\x9f\x91\x89"));
        CHECK(!strcmp(item->text_style.requested_family, "Aptos"));
        CHECK(item->text_style.font_size_dip == 96.0);
        CHECK(item->angle_deg == 0.0 && item->opacity == 1.0);
        CHECK(item->text_style.color[0] == 0 && item->text_style.color[3] == 255);
        PPAsset *font = pp_asset_retain(assets, item->text_style.fallback_asset);
        PPFontInfo info;
        CHECK(font && pp_font_inspect(pp_asset_source(font), item->text_style.fallback_face, &info, &error));
        CHECK(!strcmp(info.family, "Noto Color Emoji"));
        pp_asset_release(font);
    }
    PPAnchor anchor = PP_ANCHOR_TOP_LEFT;
    PPItem item = *pp_project_target(project, anchor);
    uint64_t original = item.id, second;
    item.id = 0;
    CHECK(pp_project_add_item(project, anchor, &item, &second, &error));
    CHECK(second != original);
    CHECK(pp_project_reorder_item(project, anchor, second, 0, &error));
    CHECK(pp_project_target(project, anchor)->id == original);
    CHECK(pp_project_remove_item(project, anchor, original, &error));
    CHECK(pp_project_target(project, anchor)->id == second);
    CHECK(!pp_project_reorder_item(project, anchor, second, 1, &error));
    CHECK(!pp_project_set_default(project, anchor, original, &error));
    CHECK(pp_project_set_enabled(project, anchor, false, &error));
    CHECK(!pp_project_target(project, anchor));
    CHECK(pp_project_set_enabled(project, anchor, true, &error));
    item = *pp_project_target(project, anchor);
    item.opacity = NAN;
    CHECK(!pp_project_edit_item(project, second, &item, &error));
    CHECK(pp_project_target(project, anchor)->opacity == 1.0);
    item.opacity = 0; item.text_style.font_size_dip = 4097;
    CHECK(!pp_project_edit_item(project, second, &item, &error));
    item.text_style.font_size_dip = 0;
    CHECK(!pp_project_edit_item(project, second, &item, &error));
    item.text_style.font_size_dip = 96;
    PPItem invalid = item;
    invalid.text = "\xc0\xaf";
    CHECK(!pp_project_edit_item(project, second, &invalid, &error));
    invalid.text = " \t";
    CHECK(!pp_project_edit_item(project, second, &invalid, &error));
    invalid = item;
    invalid.text_style.fallback_face = -1;
    CHECK(!pp_project_edit_item(project, second, &invalid, &error));
    invalid = item;
    invalid.text_style.fallback_asset = (PPAssetId){{1}};
    CHECK(!pp_project_edit_item(project, second, &invalid, &error));
    CHECK(!pp_project_add_item(project, anchor, &item, NULL, &error)); /* Existing ID. */
    CHECK(pp_project_edit_item(project, second, &item, &error));
    CHECK(pp_project_target(project, anchor)->opacity == 0);
    item = *pp_project_target(project, anchor);
    item.id = 0;
    /* Copying inputs must own their text/font references before removal. */
    PPItemSnapshot *snapshot = pp_project_snapshot(project, second);
    CHECK(snapshot);
    CHECK(pp_project_remove_item(project, anchor, second, &error));
    CHECK(!pp_project_target(project, anchor));
    CHECK(pp_project_add_item(project, anchor, &item, &second, &error));
    CHECK(pp_project_target(project, anchor)->id == second);
    pp_item_snapshot_release(snapshot);
    CHECK(!pp_project_add_item(project, PP_ANCHOR_COUNT, &item, NULL, &error));
    PPProjectSettings candidate = *pp_project_settings(project);
    candidate.easing_seconds = NAN;
    CHECK(!pp_project_set_settings(project, &candidate, &error));
    CHECK(pp_project_settings(project)->easing_seconds == .12);
    candidate.easing_seconds = .2;
    CHECK(pp_project_set_settings(project, &candidate, &error));
    CHECK(pp_project_validate(project, &error));
    pp_project_destroy(project);
    CHECK(pp_assets_external_raster_bytes(assets) == 0);
    pp_assets_destroy(assets);
}
int main(void) { test_project_defaults(); puts("PASS shared defaults, list mutations, validation and ownership"); return 0; }
