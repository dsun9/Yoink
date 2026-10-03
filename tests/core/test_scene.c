#include "pointy/scene.h"
#include "../test.h"
#include <math.h>
#include <string.h>

static PPAssetId svg(PPAssets *assets, const char *source) {
    PPAssetId id;
    PPError error = {0};
    CHECK(pp_asset_import(assets, (PPBytes){(const uint8_t *)source, strlen(source)}, PP_ASSET_SVG, &id, &error));
    return id;
}
static void test_scene_replacement_and_snapshot(void) {
    PPAssets *assets = NULL; PPProject *project = NULL; PPScene *scene = NULL;
    PPError error = {0}; uint64_t item_id, first, second;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_create(assets, &project, &error));
    CHECK(pp_scene_create(&scene, &error));
    CHECK(!pp_scene_spawn(scene, project, PP_ANCHOR_TOP_LEFT, (PPVec2){0,0}, 0, NULL, &error));
    PPItem landscape = {0};
    landscape.kind = PP_ITEM_IMAGE; landscape.image_longest_dip = 200;
    landscape.angle_deg = 35; landscape.opacity = .6;
    landscape.image_asset = svg(assets, "<svg width='200' height='100'><rect width='200' height='100' fill='red'/></svg>");
    PPItem invalid = landscape;
    invalid.image_longest_dip = 4097;
    CHECK(!pp_project_add_item(project, PP_ANCHOR_CENTER_RIGHT, &invalid, NULL, &error));
    invalid.image_longest_dip = 0;
    CHECK(!pp_project_add_item(project, PP_ANCHOR_CENTER_RIGHT, &invalid, NULL, &error));
    PPItem portrait = landscape;
    portrait.image_asset = svg(assets, "<svg width='50' height='100'><rect width='50' height='100' fill='blue'/></svg>");
    CHECK(pp_project_add_item(project, PP_ANCHOR_CENTER_RIGHT, &landscape, &item_id, &error));
    CHECK(pp_project_set_enabled(project, PP_ANCHOR_CENTER_RIGHT, false, &error));
    CHECK(!pp_scene_spawn(scene, project, PP_ANCHOR_CENTER_RIGHT, (PPVec2){300,400}, 1, NULL, &error));
    CHECK(pp_project_set_enabled(project, PP_ANCHOR_CENTER_RIGHT, true, &error));
    CHECK(pp_scene_spawn(scene, project, PP_ANCHOR_CENTER_RIGHT, (PPVec2){300,400}, 1, &first, &error));
    CHECK(pp_scene_spawn(scene, project, PP_ANCHOR_CENTER_RIGHT, (PPVec2){100,200}, 2, &second, &error));
    CHECK(pp_scene_get(scene, first)->snapshot == pp_scene_get(scene, second)->snapshot);
    CHECK(pp_scene_replace(scene, first, &portrait, 3, &error));
    const PPSceneInstance *instance = pp_scene_get(scene, first);
    CHECK(instance->pose.center.x == 300 && instance->pose.center.y == 400);
    CHECK(instance->pose.angle_deg == 35 && instance->pose.opacity == .6 && instance->pose.longest_dip == 200);
    CHECK(instance->origin_anchor == PP_ANCHOR_CENTER_RIGHT && instance->animation_start == 3);
    CHECK(pp_project_target(project, PP_ANCHOR_CENTER_RIGHT)->id == item_id);
    PPVec2 size = pp_scene_size(instance);
    CHECK(size.x == 100 && size.y == 200);
    CHECK(pp_project_edit_item(project, item_id, &portrait, &error));
    CHECK(pp_project_remove_item(project, PP_ANCHOR_CENTER_RIGHT, item_id, &error));
    pp_project_destroy(project); project = NULL;
    const PPFrame *frame; double next;
    CHECK(pp_item_snapshot_frame(pp_scene_get(scene, second)->snapshot, 0, 1, &frame, &next, &error));
    CHECK(frame->width == 200 && frame->height == 100 && frame->pixels[0] == 255);
    /* SVG cache is borrowed: request again after another size invalidates it. */
    CHECK(pp_item_snapshot_frame(pp_scene_get(scene, second)->snapshot, 0, 2, &frame, &next, &error));
    CHECK(frame->width == 400 && frame->height == 200);
    CHECK(pp_item_snapshot_frame(pp_scene_get(scene, second)->snapshot, 0, 1, &frame, &next, &error));
    CHECK(frame->width == 200);
    PPItem bad = portrait; bad.image_asset = (PPAssetId){0};
    CHECK(!pp_scene_replace(scene, first, &bad, 4, &error));
    CHECK(pp_scene_get(scene, first)->animation_start == 3);
    CHECK(!pp_scene_replace(scene, first, &portrait, NAN, &error));
    PPPose pose = instance->pose; pose.opacity = 0;
    CHECK(pp_scene_set_pose(scene, first, pose, &error));
    CHECK(!pp_scene_interactive(pp_scene_get(scene, first)));
    pose.longest_dip = 4097;
    CHECK(!pp_scene_set_pose(scene, first, pose, &error));
    pose.longest_dip = 1; pose.center.x = INFINITY;
    CHECK(!pp_scene_set_pose(scene, first, pose, &error));
    CHECK(!pp_item_snapshot_frame(instance->snapshot, 0, 1000, &frame, &next, &error));
    CHECK(!frame);
    pp_scene_delete(scene, first); CHECK(!pp_scene_get(scene, first));
    pp_scene_clear(scene); CHECK(pp_scene_count(scene) == 0);
    pp_scene_destroy(scene); pp_assets_destroy(assets);
}
static void test_text_sharing_and_raster_budget(void) {
    PPAssets *assets=NULL; PPProject *project=NULL; PPScene *scene=NULL; PPError error={0};
    CHECK(pp_assets_create(&assets,&error));
    CHECK(pp_project_default(assets,&project,&error));
    CHECK(pp_scene_create(&scene,&error));
    size_t original_bytes=pp_assets_external_raster_bytes(assets);
    CHECK(original_bytes>0);
    uint64_t ids[32];
    for(int i=0;i<32;i++) CHECK(pp_scene_spawn(scene,project,PP_ANCHOR_TOP_LEFT,(PPVec2){i,i},1,&ids[i],&error));
    CHECK(pp_assets_external_raster_bytes(assets)==original_bytes);
    PPItemSnapshot *snapshot=pp_scene_get(scene,ids[0])->snapshot;
    const PPFrame *frame=NULL;double next;
    CHECK(pp_item_snapshot_frame(snapshot,0,1,&frame,&next,&error));
    const uint8_t *old_pixels=frame->pixels;
    size_t old_bytes=frame->stride*(size_t)frame->height+(size_t)frame->width*frame->height;
    const size_t chunk=(size_t)256*1024*1024, total=(size_t)512*1024*1024;
    CHECK(!pp_assets_reserve_raster(assets,chunk+1,&error));
    CHECK(pp_assets_reserve_raster(assets,chunk,&error));
    size_t rest=total-pp_assets_raster_bytes(assets);
    CHECK(pp_assets_reserve_raster(assets,rest,&error));
    PPItem replacement=*pp_item_snapshot_item(snapshot);
    CHECK(!pp_scene_replace(scene,ids[0],&replacement,4,&error));
    CHECK(pp_scene_get(scene,ids[0])->snapshot==snapshot && pp_scene_get(scene,ids[0])->animation_start==1);
    CHECK(!pp_item_snapshot_frame(snapshot,0,2,&frame,&next,&error));
    CHECK(!frame && error.code==PP_ERROR_RESOURCE_LIMIT);
    CHECK(pp_item_snapshot_frame(snapshot,0,1,&frame,&next,&error));
    CHECK(frame->pixels==old_pixels);
    const char *small="<svg width='2' height='2'><rect width='2' height='2'/></svg>";
    PPAssetId rejected;
    CHECK(!pp_asset_import(assets,(PPBytes){(const uint8_t *)small,strlen(small)},PP_ASSET_SVG,&rejected,&error));
    CHECK(error.code==PP_ERROR_RESOURCE_LIMIT);
    pp_assets_release_raster(assets,rest);pp_assets_release_raster(assets,chunk);
    CHECK(pp_item_snapshot_frame(snapshot,0,2,&frame,&next,&error));
    size_t new_bytes=frame->stride*(size_t)frame->height+(size_t)frame->width*frame->height;
    CHECK(new_bytes>old_bytes);
    CHECK(pp_assets_external_raster_bytes(assets)==original_bytes-old_bytes+new_bytes);
    pp_project_destroy(project);
    CHECK(pp_assets_external_raster_bytes(assets)==new_bytes);
    for(int i=0;i<31;i++) pp_scene_delete(scene,ids[i]);
    CHECK(pp_assets_external_raster_bytes(assets)==new_bytes);
    CHECK(pp_item_snapshot_frame(pp_scene_get(scene,ids[31])->snapshot,0,2,&frame,&next,&error));
    pp_scene_clear(scene);CHECK(pp_assets_external_raster_bytes(assets)==0);
    pp_scene_destroy(scene);pp_assets_destroy(assets);
}
int main(void) {
    test_scene_replacement_and_snapshot(); test_text_sharing_and_raster_budget();
    puts("PASS independent replacement, immutable sharing, ownership and combined raster budget"); return 0;
}
