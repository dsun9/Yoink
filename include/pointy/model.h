#ifndef POINTY_MODEL_H
#define POINTY_MODEL_H
#include "pointy/display.h"
#include "pointy/text.h"

#define PP_ITEM_MIN_SIZE 1.0
#define PP_ITEM_MAX_SIZE 4096.0
#define PP_PROJECT_NAME_MAX 255
typedef enum PPItemKind { PP_ITEM_IMAGE, PP_ITEM_TEXT } PPItemKind;
/* Input strings are borrowed for a call; projects/snapshots copy them. Project
 * item IDs are stable and nonzero. Ad-hoc snapshot inputs may use zero.
 * Add assigns an ID for zero, or preserves an unused ID for
 * loading. Edit preserves the original ID. Text size is canonical font size;
 * image size is canonical longest unrotated dimension, both in logical DIP. */
typedef struct PPItem {
    uint64_t id;
    PPItemKind kind;
    PPAssetId image_asset;
    const char *text;
    PPTextStyle text_style;
    double image_longest_dip, angle_deg, opacity;
} PPItem;
typedef struct PPAnchorList {
    bool enabled;
    const uint64_t *item_ids;
    size_t count;
    uint64_t default_id;
} PPAnchorList;
typedef struct PPProjectSettings {
    double activation_dip, rotation_step_deg, resize_factor, easing_seconds;
} PPProjectSettings;
typedef struct PPProject PPProject;
typedef struct PPItemSnapshot PPItemSnapshot;
/* All calls are single-threaded. App-owned assets outlive all projects,
 * snapshots and scenes. Project accessors borrow until the next mutation. */
bool pp_project_create(PPAssets *assets, PPProject **out, PPError *error);
bool pp_project_default(PPAssets *assets, PPProject **out, PPError *error);
bool pp_project_validate(const PPProject *project, PPError *error);
void pp_project_destroy(PPProject *project);
bool pp_project_add_item(PPProject *project, PPAnchor anchor, const PPItem *item, uint64_t *out_id, PPError *error);
bool pp_project_remove_item(PPProject *project, PPAnchor anchor, uint64_t item_id, PPError *error);
bool pp_project_set_default(PPProject *project, PPAnchor anchor, uint64_t item_id, PPError *error);
bool pp_project_reorder_item(PPProject *project, PPAnchor anchor, uint64_t item_id, size_t index, PPError *error);
bool pp_project_edit_item(PPProject *project, uint64_t item_id, const PPItem *replacement, PPError *error);
bool pp_project_set_enabled(PPProject *project, PPAnchor anchor, bool enabled, PPError *error);
bool pp_project_set_settings(PPProject *project, const PPProjectSettings *settings, PPError *error);
const PPProjectSettings *pp_project_settings(const PPProject *project);
const PPAnchorList *pp_project_anchor(const PPProject *project, PPAnchor anchor);
const PPItem *pp_project_item(const PPProject *project, uint64_t id);
const PPItem *pp_project_target(const PPProject *project, PPAnchor anchor);
PPAssets *pp_project_assets(const PPProject *project);
uint64_t pp_project_revision(const PPProject *project);
/* Owned UTF-8 name, independent of filename. 1..255 bytes, no control characters
 * or whitespace-only names. Borrowed accessor expires on mutation/destruction. */
bool pp_project_set_name(PPProject *project, const char *name, PPError *error);
const char *pp_project_name(const PPProject *project);
/* Project-specific UTF-8 attribution keyed by source digest, copied on set.
 * A NULL/empty notice clears it; getter borrows until mutation/destruction. */
bool pp_project_set_notice(PPProject *project, PPAssetId id, const char *notice, PPError *error);
const char *pp_project_notice(const PPProject *project, PPAssetId id);
/* Returns a retained snapshot, valid through project edits/destruction. */
PPItemSnapshot *pp_project_snapshot(const PPProject *project, uint64_t id);
bool pp_item_snapshot_create(PPAssets *assets, const PPItem *item, PPItemSnapshot **out, PPError *error);
PPItemSnapshot *pp_item_snapshot_retain(PPItemSnapshot *snapshot);
void pp_item_snapshot_release(PPItemSnapshot *snapshot);
const PPItem *pp_item_snapshot_item(const PPItemSnapshot *snapshot);
PPVec2 pp_item_snapshot_size(const PPItemSnapshot *snapshot);
PPAssets *pp_item_snapshot_assets(const PPItemSnapshot *snapshot);
/* Process-local immutable content revision. Retains preserve it; a freshly
 * created snapshot has a distinct revision, including after same-item edits. */
uint64_t pp_item_snapshot_revision(const PPItemSnapshot *snapshot);
bool pp_item_snapshot_select_frame(PPItemSnapshot *snapshot,double elapsed,double scale,
    PPFrameSelection *out,PPError *error);
/* Cached pixels are borrowed until the next request on this snapshot or asset.
 * SVG calls on ANY snapshot of the same asset may invalidate the frame. Consume
 * immediately or request again. Text cache bytes include RGBA plus coverage and
 * reserve against the manager's shared 512 MiB raster budget. Returned text
 * frames are shared by all retainers, never copied per scene instance.
 * scale is pixels per canonical logical DIP. Image PNG/GIF frames stay native;
 * SVG/text rerasterize at scale. Per-resource/dimension limits still apply. */
bool pp_item_snapshot_frame(PPItemSnapshot *snapshot, double elapsed, double scale,
    const PPFrame **out, double *next, PPError *error);
#endif
