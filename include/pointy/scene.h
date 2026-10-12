#ifndef POINTY_SCENE_H
#define POINTY_SCENE_H
#include "pointy/model.h"
#include "pointy/interaction.h"

typedef struct PPScene PPScene;
typedef struct PPSceneInstance {
    uint64_t id;
    PPItemSnapshot *snapshot;
    PPPose pose;
    PPAnchor origin_anchor;
    double animation_start;
} PPSceneInstance;
bool pp_scene_create(PPScene **out, PPError *error);
void pp_scene_destroy(PPScene *scene);
bool pp_scene_spawn(PPScene *scene, const PPProject *project, PPAnchor anchor,
    PPVec2 center, double now, uint64_t *out_id, PPError *error);
bool pp_scene_replace(PPScene *scene, uint64_t instance_id, const PPItem *replacement, double now, PPError *error);
void pp_scene_delete(PPScene *scene, uint64_t instance_id);
void pp_scene_clear(PPScene *scene);
bool pp_scene_set_pose(PPScene *scene, uint64_t id, PPPose pose, PPError *error);
/* Restore a retained immutable snapshot after a rejected candidate. Borrows
 * and retains snapshot; no source regeneration or project mutation. */
bool pp_scene_restore(PPScene *scene,uint64_t id,PPItemSnapshot *snapshot,
    PPPose pose,double animation_start,PPError *error);
/* Borrowed views, invalidated by any scene mutation. IDs are never reused,
 * including after clear. Scene changes never modify the shared project. */
const PPSceneInstance *pp_scene_get(const PPScene *scene, uint64_t id);
const PPSceneInstance *pp_scene_at(const PPScene *scene, size_t index);
size_t pp_scene_count(const PPScene *scene);
PPVec2 pp_scene_size(const PPSceneInstance *instance);
bool pp_scene_interactive(const PPSceneInstance *instance);
#endif
