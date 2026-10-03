#include "pointy/scene.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PPScene {
    PPSceneInstance *instances;
    size_t count;
    uint64_t next_id;
};
static bool scene_error(PPError *error, int code, const char *message) {
    if (error) {
        error->code = code;
        snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return false;
}
static size_t find(const PPScene *scene, uint64_t id) {
    for (size_t i = 0; scene && i < scene->count; i++) if (scene->instances[i].id == id) return i;
    return SIZE_MAX;
}
static bool valid_pose(PPPose pose) {
    return isfinite(pose.center.x) && isfinite(pose.center.y) && isfinite(pose.angle_deg) &&
        isfinite(pose.opacity) && pose.opacity >= 0 && pose.opacity <= 1 &&
        isfinite(pose.longest_dip) && pose.longest_dip >= PP_ITEM_MIN_SIZE && pose.longest_dip <= PP_ITEM_MAX_SIZE;
}
bool pp_scene_create(PPScene **out, PPError *error) {
    if (!out) return scene_error(error, PP_ERROR_ARGUMENT, "Missing scene output");
    *out = calloc(1, sizeof(**out));
    if (!*out) return scene_error(error, PP_ERROR_MEMORY, "Cannot allocate scene");
    (*out)->next_id = 1;
    return true;
}
void pp_scene_clear(PPScene *scene) {
    if (!scene) return;
    for (size_t i = 0; i < scene->count; i++) pp_item_snapshot_release(scene->instances[i].snapshot);
    free(scene->instances);
    scene->instances = NULL;
    scene->count = 0;
}
void pp_scene_destroy(PPScene *scene) { pp_scene_clear(scene); free(scene); }
bool pp_scene_spawn(PPScene *scene, const PPProject *project, PPAnchor anchor,
    PPVec2 center, double now, uint64_t *out_id, PPError *error) {
    const PPItem *item = pp_project_target(project, anchor);
    if (!scene || !item || !isfinite(center.x) || !isfinite(center.y) || !isfinite(now) || now < 0)
        return scene_error(error, PP_ERROR_ARGUMENT, "Invalid spawn position, clock or empty/disabled anchor");
    if (!scene->next_id || scene->next_id == UINT64_MAX || scene->count >= SIZE_MAX / sizeof(*scene->instances) - 1)
        return scene_error(error, PP_ERROR_RESOURCE_LIMIT, "Scene IDs or capacity exhausted");
    PPSceneInstance *instances = realloc(scene->instances, (scene->count + 1) * sizeof(*instances));
    if (!instances) return scene_error(error, PP_ERROR_MEMORY, "Cannot grow scene");
    scene->instances = instances;
    PPItemSnapshot *snapshot = pp_project_snapshot(project, item->id);
    PPVec2 size = pp_item_snapshot_size(snapshot);
    double longest = fmin(PP_ITEM_MAX_SIZE, fmax(PP_ITEM_MIN_SIZE, fmax(size.x, size.y)));
    PPSceneInstance instance = {scene->next_id++, snapshot, {center, longest, item->angle_deg, item->opacity}, anchor, now};
    instances[scene->count++] = instance;
    if (out_id) *out_id = instance.id;
    return true;
}
bool pp_scene_replace(PPScene *scene, uint64_t id, const PPItem *replacement, double now, PPError *error) {
    size_t at = find(scene, id);
    if (at == SIZE_MAX || !replacement || !isfinite(now) || now < 0)
        return scene_error(error, PP_ERROR_ARGUMENT, "Invalid replacement, instance or clock");
    PPSceneInstance *instance = scene->instances + at;
    PPItemSnapshot *snapshot = NULL;
    if (!pp_item_snapshot_create(pp_item_snapshot_assets(instance->snapshot), replacement, &snapshot, error)) return false;
    pp_item_snapshot_release(instance->snapshot);
    instance->snapshot = snapshot;
    instance->animation_start = now;
    return true;
}
void pp_scene_delete(PPScene *scene, uint64_t id) {
    size_t at = find(scene, id);
    if (at == SIZE_MAX) return;
    pp_item_snapshot_release(scene->instances[at].snapshot);
    memmove(scene->instances + at, scene->instances + at + 1, (scene->count - at - 1) * sizeof(*scene->instances));
    scene->count--;
}
bool pp_scene_set_pose(PPScene *scene, uint64_t id, PPPose pose, PPError *error) {
    size_t at = find(scene, id);
    if (at == SIZE_MAX || !valid_pose(pose)) return scene_error(error, PP_ERROR_ARGUMENT, "Invalid scene pose or instance");
    scene->instances[at].pose = pose;
    return true;
}
const PPSceneInstance *pp_scene_get(const PPScene *scene, uint64_t id) {
    size_t at = find(scene, id);
    return at == SIZE_MAX ? NULL : scene->instances + at;
}
const PPSceneInstance *pp_scene_at(const PPScene *scene, size_t index) {
    return scene && index < scene->count ? scene->instances + index : NULL;
}
size_t pp_scene_count(const PPScene *scene) { return scene ? scene->count : 0; }
PPVec2 pp_scene_size(const PPSceneInstance *instance) {
    if (!instance) return (PPVec2){0};
    PPVec2 size = pp_item_snapshot_size(instance->snapshot);
    double factor = instance->pose.longest_dip / fmax(size.x, size.y);
    return (PPVec2){size.x * factor, size.y * factor};
}
bool pp_scene_interactive(const PPSceneInstance *instance) { return instance && instance->pose.opacity > 0; }
