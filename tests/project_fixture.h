#ifndef POINTY_TEST_PROJECT_FIXTURE_H
#define POINTY_TEST_PROJECT_FIXTURE_H
#include "pointy/model.h"
#include "test.h"
#include <string.h>
/* Stable arrow shape for existing input/display geometry tests. The real
 * default hand/font resources are tested by test_model and test_text. */
static PPProject *test_arrow_project(PPAssets *assets) {
    const char *svg = "<svg width='64' height='64'><rect x='8' y='26' width='30' height='12' fill='red'/><path d='M32 13 L58 32 L32 52 Z' fill='red'/></svg>";
    PPItem item = {0};
    PPError error = {0};
    item.kind = PP_ITEM_IMAGE;
    item.image_longest_dip = 96;
    item.opacity = 1;
    CHECK(pp_asset_import(assets, (PPBytes){(const uint8_t *)svg, strlen(svg)}, PP_ASSET_SVG, &item.image_asset, &error));
    PPProject *project = NULL;
    CHECK(pp_project_create(assets, &project, &error));
    for (int a = 0; a < PP_ANCHOR_COUNT; a++) CHECK(pp_project_add_item(project, (PPAnchor)a, &item, NULL, &error));
    return project;
}
#endif
