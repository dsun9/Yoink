#include "pointy/model.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PPProject {
    PPAssets *assets;
    PPAnchorList anchors[PP_ANCHOR_COUNT];
    PPItemSnapshot **items;
    size_t count;
    uint64_t next_id, revision;
    PPProjectSettings settings;
};

static bool project_error(PPError *error, int code, const char *message) {
    if (error) {
        error->code = code;
        snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return false;
}
static bool valid_anchor(PPAnchor anchor) { return anchor >= 0 && anchor < PP_ANCHOR_COUNT; }
static size_t item_index(const PPProject *project, uint64_t id) {
    for (size_t i = 0; project && i < project->count; i++)
        if (pp_item_snapshot_item(project->items[i])->id == id) return i;
    return SIZE_MAX;
}
static size_t list_index(const PPAnchorList *list, uint64_t id) {
    for (size_t i = 0; i < list->count; i++) if (list->item_ids[i] == id) return i;
    return SIZE_MAX;
}
static bool valid_settings(const PPProjectSettings *settings) {
    return settings && isfinite(settings->activation_dip) && settings->activation_dip >= 1 && settings->activation_dip <= 4096 &&
        isfinite(settings->rotation_step_deg) && settings->rotation_step_deg > 0 && settings->rotation_step_deg <= 360 &&
        isfinite(settings->resize_factor) && settings->resize_factor > 1 && settings->resize_factor <= 2 &&
        isfinite(settings->easing_seconds) && settings->easing_seconds >= 0 && settings->easing_seconds <= 10;
}

bool pp_project_create(PPAssets *assets, PPProject **out, PPError *error) {
    if (out) *out = NULL;
    if (!assets || !out) return project_error(error, PP_ERROR_ARGUMENT, "Missing project asset manager or output");
    PPProject *project = calloc(1, sizeof(*project));
    if (!project) return project_error(error, PP_ERROR_MEMORY, "Cannot allocate project");
    project->assets = assets;
    project->next_id = 1;
    project->revision = 1;
    project->settings = (PPProjectSettings){16, 10, 1.1, .12};
    for (int a = 0; a < PP_ANCHOR_COUNT; a++) project->anchors[a].enabled = true;
    *out = project;
    return true;
}

bool pp_project_default(PPAssets *assets, PPProject **out, PPError *error) {
    if (out) *out = NULL;
    if (!out) return project_error(error, PP_ERROR_ARGUMENT, "Missing project output");
    PPProject *project = NULL;
    if (!pp_project_create(assets, &project, error)) return false;
    PPItem item = {0};
    item.kind = PP_ITEM_TEXT;
    item.text = "\xf0\x9f\x91\x89";
    item.opacity = 1;
    if (!pp_text_default_style(assets, &item.text_style, error)) goto failed;
    for (int a = 0; a < PP_ANCHOR_COUNT; a++)
        if (!pp_project_add_item(project, (PPAnchor)a, &item, NULL, error)) goto failed;
    *out = project;
    return true;
failed:
    pp_project_destroy(project);
    return false;
}

void pp_project_destroy(PPProject *project) {
    if (!project) return;
    for (size_t i = 0; i < project->count; i++) pp_item_snapshot_release(project->items[i]);
    for (int a = 0; a < PP_ANCHOR_COUNT; a++) free((void *)project->anchors[a].item_ids);
    free(project->items);
    free(project);
}

bool pp_project_validate(const PPProject *project, PPError *error) {
    if (!project || !project->assets || !valid_settings(&project->settings))
        return project_error(error, PP_ERROR_ARGUMENT, "Invalid project settings");
    size_t count = 0;
    for (int a = 0; a < PP_ANCHOR_COUNT; a++) {
        const PPAnchorList *list = &project->anchors[a];
        if ((list->count && list_index(list, list->default_id) == SIZE_MAX) || (!list->count && list->default_id))
            return project_error(error, PP_ERROR_ARGUMENT, "Default item is absent from its anchor");
        for (size_t i = 0; i < list->count; i++) {
            if (!list->item_ids[i] || item_index(project, list->item_ids[i]) == SIZE_MAX)
                return project_error(error, PP_ERROR_ARGUMENT, "Anchor references an invalid item");
        }
        count += list->count;
    }
    if (count != project->count) return project_error(error, PP_ERROR_ARGUMENT, "Orphan item definitions");
    /* Content is validated transactionally by snapshot creation. No writable
     * content/list view escapes this boundary. */
    return true;
}

bool pp_project_add_item(PPProject *project, PPAnchor anchor, const PPItem *item,
    uint64_t *out_id, PPError *error) {
    if (!project || !valid_anchor(anchor) || !item)
        return project_error(error, PP_ERROR_ARGUMENT, "Invalid project, anchor or item");
    PPItem candidate = *item;
    candidate.id = item->id ? item->id : project->next_id;
    if (!candidate.id || candidate.id == UINT64_MAX || item_index(project, candidate.id) != SIZE_MAX)
        return project_error(error, PP_ERROR_ARGUMENT, "Item ID is duplicate or exhausted");
    PPItemSnapshot *snapshot = NULL;
    if (!pp_item_snapshot_create(project->assets, &candidate, &snapshot, error)) return false;
    PPAnchorList *list = &project->anchors[anchor];
    if (project->count >= SIZE_MAX / sizeof(*project->items) - 1 || list->count >= SIZE_MAX / sizeof(uint64_t) - 1) {
        pp_item_snapshot_release(snapshot);
        return project_error(error, PP_ERROR_RESOURCE_LIMIT, "Too many project items");
    }
    PPItemSnapshot **items = malloc((project->count + 1) * sizeof(*items));
    uint64_t *ids = malloc((list->count + 1) * sizeof(*ids));
    if (!items || !ids) {
        free(items); free(ids); pp_item_snapshot_release(snapshot);
        return project_error(error, PP_ERROR_MEMORY, "Cannot grow project list");
    }
    if (project->count) memcpy(items, project->items, project->count * sizeof(*items));
    if (list->count) memcpy(ids, list->item_ids, list->count * sizeof(*ids));
    items[project->count] = snapshot;
    ids[list->count] = candidate.id;
    free(project->items); free((void *)list->item_ids);
    project->items = items; project->count++;
    list->item_ids = ids;
    if (!list->count++) list->default_id = candidate.id;
    if (candidate.id >= project->next_id) project->next_id = candidate.id + 1;
    project->revision++;
    if (out_id) *out_id = candidate.id;
    return true;
}

bool pp_project_remove_item(PPProject *project, PPAnchor anchor, uint64_t id, PPError *error) {
    if (!project || !valid_anchor(anchor)) return project_error(error, PP_ERROR_ARGUMENT, "Invalid anchor");
    PPAnchorList *list = &project->anchors[anchor];
    size_t at = list_index(list, id), item_at = item_index(project, id);
    if (at == SIZE_MAX || item_at == SIZE_MAX) return project_error(error, PP_ERROR_ARGUMENT, "Item is absent from anchor");
    uint64_t *ids = (uint64_t *)list->item_ids;
    memmove(ids + at, ids + at + 1, (list->count - at - 1) * sizeof(*ids));
    list->count--;
    if (list->default_id == id) list->default_id = list->count ? ids[0] : 0;
    pp_item_snapshot_release(project->items[item_at]);
    memmove(project->items + item_at, project->items + item_at + 1,
        (project->count - item_at - 1) * sizeof(*project->items));
    project->count--; project->revision++;
    return true;
}

bool pp_project_set_default(PPProject *project, PPAnchor anchor, uint64_t id, PPError *error) {
    if (!project || !valid_anchor(anchor) || list_index(&project->anchors[anchor], id) == SIZE_MAX)
        return project_error(error, PP_ERROR_ARGUMENT, "Default item is absent from anchor");
    project->anchors[anchor].default_id = id;
    project->revision++;
    return true;
}

bool pp_project_reorder_item(PPProject *project, PPAnchor anchor, uint64_t id, size_t index, PPError *error) {
    if (!project || !valid_anchor(anchor)) return project_error(error, PP_ERROR_ARGUMENT, "Invalid anchor");
    PPAnchorList *list = &project->anchors[anchor];
    size_t from = list_index(list, id);
    if (from == SIZE_MAX || index >= list->count) return project_error(error, PP_ERROR_ARGUMENT, "Reorder index or item is absent");
    uint64_t *ids = (uint64_t *)list->item_ids;
    if (from < index) memmove(ids + from, ids + from + 1, (index - from) * sizeof(*ids));
    else if (from > index) memmove(ids + index + 1, ids + index, (from - index) * sizeof(*ids));
    ids[index] = id;
    project->revision++;
    return true;
}

bool pp_project_edit_item(PPProject *project, uint64_t id, const PPItem *replacement, PPError *error) {
    size_t at = item_index(project, id);
    if (at == SIZE_MAX || !replacement) return project_error(error, PP_ERROR_ARGUMENT, "Edit item is absent");
    PPItem candidate = *replacement;
    candidate.id = id;
    PPItemSnapshot *snapshot = NULL;
    if (!pp_item_snapshot_create(project->assets, &candidate, &snapshot, error)) return false;
    pp_item_snapshot_release(project->items[at]);
    project->items[at] = snapshot;
    project->revision++;
    return true;
}

bool pp_project_set_enabled(PPProject *project, PPAnchor anchor, bool enabled, PPError *error) {
    if (!project || !valid_anchor(anchor)) return project_error(error, PP_ERROR_ARGUMENT, "Invalid anchor");
    project->anchors[anchor].enabled = enabled;
    project->revision++;
    return true;
}
bool pp_project_set_settings(PPProject *project, const PPProjectSettings *settings, PPError *error) {
    if (!project || !valid_settings(settings)) return project_error(error, PP_ERROR_ARGUMENT, "Invalid interaction settings");
    project->settings = *settings;
    project->revision++;
    return true;
}
const PPProjectSettings *pp_project_settings(const PPProject *project) { return project ? &project->settings : NULL; }
const PPAnchorList *pp_project_anchor(const PPProject *project, PPAnchor anchor) {
    return project && valid_anchor(anchor) ? &project->anchors[anchor] : NULL;
}
const PPItem *pp_project_item(const PPProject *project, uint64_t id) {
    size_t at = item_index(project, id);
    return at == SIZE_MAX ? NULL : pp_item_snapshot_item(project->items[at]);
}
const PPItem *pp_project_target(const PPProject *project, PPAnchor anchor) {
    const PPAnchorList *list = pp_project_anchor(project, anchor);
    return list && list->enabled ? pp_project_item(project, list->default_id) : NULL;
}
PPAssets *pp_project_assets(const PPProject *project) { return project ? project->assets : NULL; }
uint64_t pp_project_revision(const PPProject *project) { return project ? project->revision : 0; }
PPItemSnapshot *pp_project_snapshot(const PPProject *project, uint64_t id) {
    size_t at = item_index(project, id);
    return at == SIZE_MAX ? NULL : pp_item_snapshot_retain(project->items[at]);
}
