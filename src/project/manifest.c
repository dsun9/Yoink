#include "store_internal.h"
#include "../media/fonts_internal.h"
#include "pointy/sha256.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static const char *kind_names[] = {"png", "gif", "svg", "font"};
static const char *embedding_names[] = {"installable", "editable", "preview_only", "restricted"};
static bool zero_id(PPAssetId id) {
    static const PPAssetId zero = {0};
    return !memcmp(&id, &zero, sizeof(id));
}
void pp_store_hex(PPAssetId id, char out[65]) {
    for (int i = 0; i < 32; i++)
        snprintf(out + 2 * i, 3, "%02x", id.bytes[i]);
}
void pp_store_resource_name(PPAssetId id, PPAssetKind kind, char out[PP_STORE_RESOURCE_NAME]) {
    const char *prefix = kind == PP_ASSET_FONT ? "fonts/" : "assets/";
    size_t length = strlen(prefix);
    memcpy(out, prefix, length);
    pp_store_hex(id, out + length);
}
bool pp_store_id(const char *text, PPAssetId *out) {
    if (!text || strlen(text) != 64)
        return false;
    for (int i = 0; i < 32; i++) {
        unsigned value = 0;
        for (int j = 0; j < 2; j++) {
            char c = text[2 * i + j];
            if (c >= '0' && c <= '9')
                value = value * 16 + c - '0';
            else if (c >= 'a' && c <= 'f')
                value = value * 16 + c - 'a' + 10;
            else
                return false;
        }
        out->bytes[i] = (uint8_t)value;
    }
    return true;
}
void pp_manifest_destroy(PPManifest *manifest) {
    for (size_t i = 0; i < manifest->count; i++)
        pp_asset_release(manifest->resources[i].asset);
    free(manifest->resources);
    yyjson_doc_free(manifest->doc);
    memset(manifest, 0, sizeof(*manifest));
}
static bool portable_font(PPAssets *assets, PPAssetId id, int face, PPError *error) {
    if (zero_id(id))
        return face == 0 ||
               pp_store_error(error, PP_ERROR_FORMAT, "Absent font must use face zero");
    PPAsset *asset = pp_asset_retain(assets, id);
    PPFontInfo info;
    bool ok = asset && pp_asset_kind(asset) == PP_ASSET_FONT &&
              pp_font_inspect(pp_asset_source(asset), face, &info, error);
    if (!ok) {
        pp_asset_release(asset);
        return pp_store_error(error, PP_ERROR_FORMAT, "Missing font or invalid collection face");
    }
    /* FreeType also accepts named-instance selectors encoded in the high bits.
     * The package schema permits only actual collection face indices. */
    if (face < 0 || (size_t)face >= info.face_count) {
        pp_asset_release(asset);
        return pp_store_error(error, PP_ERROR_FORMAT, "Font face is outside the collection range");
    }
    /* Every face travels in the complete collection. A permitted selected face
     * cannot authorize embedding a restricted sibling. Bound inspection work
     * as well as bytes for adversarial collections with repeated face offsets. */
    if (!info.face_count || info.face_count > 4096) {
        pp_asset_release(asset);
        return pp_store_error(error, PP_ERROR_RESOURCE_LIMIT, "Font collection exceeds 4096 faces");
    }
    size_t count = info.face_count;
    for (size_t i = 0; ok && i < count; i++) {
        ok = pp_font_inspect(pp_asset_source(asset), (int)i, &info, error) && info.embeddable;
    }
    pp_asset_release(asset);
    return ok ||
           pp_store_error(error, PP_ERROR_FORMAT,
                          "Every embedded collection face must permit complete editable embedding");
}
static bool portable_text(PPAssets *assets, const PPItem *item, PPError *error) {
    const PPTextStyle *style = &item->text_style;
    if (!portable_font(assets, style->primary_asset, style->primary_face, error) ||
        !portable_font(assets, style->fallback_asset, style->fallback_face, error))
        return false;
    PPFrame frame = {0};
    PPTextDiagnostics diagnostics;
    bool ok = pp_text_render(assets, item->text, style, 1, &frame, &diagnostics, error);
    pp_frame_destroy(&frame);
    if (!ok)
        return false;
    if (diagnostics.missing_glyphs)
        return pp_store_error(
            error, PP_ERROR_FORMAT,
            "Text contains unresolved glyphs; import a covering font before saving");
    if (diagnostics.missing_requested_families) {
        PPAsset *fallback = pp_asset_retain(assets, style->fallback_asset);
        PPFontInfo info;
        /* Draw diagnostics omit empty .notdef bitmaps. Check shaped glyph
         * indices too before permitting an absent primary family. */
        bool covered_emoji =
            zero_id(style->primary_asset) && style->requested_family &&
            !strcmp(style->requested_family, "Aptos") &&
            fallback &&
            pp_font_inspect(pp_asset_source(fallback), style->fallback_face, &info, error) &&
            !strcmp(info.family, "Noto Color Emoji") &&
            pp_font_resolves_text(pp_asset_source(fallback), style->fallback_face, item->text, error);
        pp_asset_release(fallback);
        if (!covered_emoji)
            return pp_store_error(error, PP_ERROR_FORMAT, "Requested font family is unresolved");
    }
    return true;
}
static bool collect_resource(const PPProject *project, PPManifest *manifest, PPAssetId id,
                             uint64_t *total, PPError *error) {
    if (zero_id(id))
        return true;
    for (size_t i = 0; i < manifest->count; i++)
        if (!memcmp(&manifest->resources[i].id, &id, sizeof(id)))
            return true;
    if (manifest->count >= PP_STORE_ENTRIES - 1)
        return pp_store_error(error, PP_ERROR_RESOURCE_LIMIT, "Too many package resources");
    PPAsset *asset = pp_asset_retain(pp_project_assets(project), id);
    PPBytes bytes = pp_asset_source(asset);
    if (!asset || !bytes.size || bytes.size > PP_STORE_RESOURCE ||
        *total > PP_STORE_TOTAL - bytes.size) {
        pp_asset_release(asset);
        return pp_store_error(error, PP_ERROR_RESOURCE_LIMIT,
                              "Source resource exceeds 64 MiB or package exceeds 512 MiB");
    }
    PPStoredResource *res = &manifest->resources[manifest->count++];
    res->id = id;
    res->kind = pp_asset_kind(asset);
    res->asset = asset;
    res->notice = pp_project_notice(project, id);
    *total += bytes.size;
    return true;
}
static yyjson_mut_val *font_json(yyjson_mut_doc *doc, PPAssetId id, int face) {
    if (zero_id(id))
        return yyjson_mut_null(doc);
    yyjson_mut_val *font = yyjson_mut_obj(doc);
    char hash[65];
    pp_store_hex(id, hash);
    if (!font || !yyjson_mut_obj_add_strcpy(doc, font, "resource", hash) ||
        !yyjson_mut_obj_add_int(doc, font, "face", face))
        return NULL;
    return font;
}
static bool count_manifest_text(const char *text, size_t *total, PPError *error) {
    size_t length = text ? strlen(text) : 0;
    if (length > PP_STORE_MANIFEST - *total)
        return pp_store_error(error, PP_ERROR_RESOURCE_LIMIT, "Manifest strings exceed 2 MiB");
    *total += length;
    return true;
}
static yyjson_mut_val *font_faces_json(yyjson_mut_doc *doc, PPAsset *asset, size_t *metadata_bytes,
                                       PPError *error) {
    PPFontInfo info;
    PPBytes bytes = pp_asset_source(asset);
    if (!pp_font_inspect(bytes, 0, &info, error))
        return NULL;
    /* Charge before allocating the array or its records. This conservatively
     * bounds resource_count * face_count even when strings fit the manifest. */
    const size_t face_bytes = 128;
    if (!info.face_count || info.face_count > 4096 ||
        info.face_count > (PP_STORE_MANIFEST - *metadata_bytes) / face_bytes) {
        pp_store_error(error, PP_ERROR_RESOURCE_LIMIT, "Font metadata exceeds manifest budget");
        return NULL;
    }
    size_t count = info.face_count;
    *metadata_bytes += count * face_bytes;
    yyjson_mut_val *faces = yyjson_mut_arr(doc);
    if (!faces)
        goto memory;
    for (size_t i = 0; i < count; i++) {
        if (!pp_font_inspect(bytes, (int)i, &info, error))
            return NULL;
        yyjson_mut_val *face = yyjson_mut_obj(doc);
        if (!face || !yyjson_mut_arr_append(faces, face) ||
            !yyjson_mut_obj_add_str(doc, face, "embedding", embedding_names[info.embedding]) ||
            !yyjson_mut_obj_add_bool(doc, face, "no_subsetting", info.no_subsetting) ||
            !yyjson_mut_obj_add_bool(doc, face, "bitmap_only", info.bitmap_only))
            goto memory;
    }
    return faces;
memory:
    pp_store_error(error, PP_ERROR_MEMORY, "Cannot construct font embedding metadata");
    return NULL;
}
#define ADD(expr)                                                                                  \
    do {                                                                                           \
        if (!(expr))                                                                               \
            goto memory;                                                                           \
    } while (0)
bool pp_manifest_write(const PPProject *project, char **json, size_t *size, PPManifest *manifest,
                       PPError *error) {
    *json = NULL;
    *size = 0;
    if (!pp_project_validate(project, error))
        return false;
    manifest->resources = calloc(PP_STORE_ENTRIES - 1, sizeof(*manifest->resources));
    if (!manifest->resources)
        return pp_store_error(error, PP_ERROR_MEMORY, "Cannot collect package resources");
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc)
        return pp_store_error(error, PP_ERROR_MEMORY, "Cannot allocate manifest");
    uint64_t total = 0;
    size_t item_count = 0, text_bytes = 0, metadata_bytes = 0;
    bool ok = false;
    yyjson_mut_val *root = yyjson_mut_obj(doc), *settings = yyjson_mut_obj(doc),
                   *lists = yyjson_mut_arr(doc), *resources = yyjson_mut_arr(doc);
    ADD(root && settings && lists && resources);
    yyjson_mut_doc_set_root(doc, root);
    ADD(yyjson_mut_obj_add_uint(doc, root, "schema", 1));
    ADD(yyjson_mut_obj_add_str(doc, root, "name", pp_project_name(project)));
    ADD(yyjson_mut_obj_add_val(doc, root, "settings", settings));
    ADD(yyjson_mut_obj_add_val(doc, root, "lists", lists));
    ADD(yyjson_mut_obj_add_val(doc, root, "resources", resources));
    const PPProjectSettings *s = pp_project_settings(project);
    ADD(yyjson_mut_obj_add_real(doc, settings, "activation_dip", s->activation_dip));
    ADD(yyjson_mut_obj_add_real(doc, settings, "rotation_step_deg", s->rotation_step_deg));
    ADD(yyjson_mut_obj_add_real(doc, settings, "resize_factor", s->resize_factor));
    ADD(yyjson_mut_obj_add_real(doc, settings, "easing_seconds", s->easing_seconds));
    for (int a = 0; a < PP_ANCHOR_COUNT; a++) {
        const PPAnchorList *list = pp_project_anchor(project, (PPAnchor)a);
        yyjson_mut_val *object = yyjson_mut_obj(doc), *items = yyjson_mut_arr(doc);
        ADD(object && items);
        ADD(yyjson_mut_arr_append(lists, object));
        ADD(yyjson_mut_obj_add_bool(doc, object, "enabled", list->enabled));
        ADD(yyjson_mut_obj_add_uint(doc, object, "default", list->default_id));
        ADD(yyjson_mut_obj_add_val(doc, object, "items", items));
        for (size_t i = 0; i < list->count; i++) {
            if (++item_count > 4096) {
                pp_store_error(error, PP_ERROR_RESOURCE_LIMIT, "Manifest exceeds 4096 items");
                goto done;
            }
            const PPItem *item = pp_project_item(project, list->item_ids[i]);
            yyjson_mut_val *value = yyjson_mut_obj(doc);
            ADD(value);
            ADD(yyjson_mut_arr_append(items, value));
            ADD(yyjson_mut_obj_add_uint(doc, value, "id", item->id));
            ADD(yyjson_mut_obj_add_str(doc, value, "kind",
                                       item->kind == PP_ITEM_IMAGE ? "image" : "text"));
            ADD(yyjson_mut_obj_add_real(doc, value, "angle", item->angle_deg));
            ADD(yyjson_mut_obj_add_real(doc, value, "opacity", item->opacity));
            if (item->kind == PP_ITEM_IMAGE) {
                if (!collect_resource(project, manifest, item->image_asset, &total, error))
                    goto done;
                char hash[65];
                pp_store_hex(item->image_asset, hash);
                ADD(yyjson_mut_obj_add_strcpy(doc, value, "resource", hash));
                ADD(yyjson_mut_obj_add_real(doc, value, "image_longest_dip",
                                            item->image_longest_dip));
            } else {
                const PPTextStyle *style = &item->text_style;
                if (!count_manifest_text(item->text, &text_bytes, error) ||
                    !count_manifest_text(style->requested_family, &text_bytes, error))
                    goto done;
                if (!portable_text(pp_project_assets(project), item, error) ||
                    !collect_resource(project, manifest, style->primary_asset, &total, error) ||
                    !collect_resource(project, manifest, style->fallback_asset, &total, error))
                    goto done;
                ADD(yyjson_mut_obj_add_str(doc, value, "text", item->text));
                ADD(yyjson_mut_obj_add_str(doc, value, "requested_family",
                                           style->requested_family ? style->requested_family : ""));
                ADD(yyjson_mut_obj_add_real(doc, value, "font_size_dip", style->font_size_dip));
                ADD(yyjson_mut_obj_add_val(
                    doc, value, "primary",
                    font_json(doc, style->primary_asset, style->primary_face)));
                ADD(yyjson_mut_obj_add_val(
                    doc, value, "fallback",
                    font_json(doc, style->fallback_asset, style->fallback_face)));
                yyjson_mut_val *color = yyjson_mut_arr(doc);
                ADD(color);
                ADD(yyjson_mut_obj_add_val(doc, value, "color", color));
                for (int c = 0; c < 4; c++)
                    ADD(yyjson_mut_arr_add_uint(doc, color, style->color[c]));
            }
        }
    }
    for (size_t i = 0; i < manifest->count; i++) {
        PPStoredResource *r = &manifest->resources[i];
        char hash[65];
        pp_store_hex(r->id, hash);
        yyjson_mut_val *value = yyjson_mut_obj(doc);
        ADD(value);
        if (!count_manifest_text(r->notice, &text_bytes, error))
            goto done;
        ADD(yyjson_mut_arr_append(resources, value));
        ADD(yyjson_mut_obj_add_strcpy(doc, value, "sha256", hash));
        ADD(yyjson_mut_obj_add_str(doc, value, "kind", kind_names[r->kind]));
        ADD(yyjson_mut_obj_add_str(doc, value, "notice", r->notice));
        if (r->kind == PP_ASSET_FONT) {
            yyjson_mut_val *faces = font_faces_json(doc, r->asset, &metadata_bytes, error);
            if (!faces)
                goto done;
            ADD(yyjson_mut_obj_add_val(doc, value, "faces", faces));
        }
    }
    *json = yyjson_mut_write(doc, 0, size);
    ADD(*json);
    if (*size > PP_STORE_MANIFEST || total > PP_STORE_TOTAL - *size) {
        free(*json);
        *json = NULL;
        pp_store_error(error, PP_ERROR_RESOURCE_LIMIT,
                       "Manifest exceeds 2 MiB or package exceeds 512 MiB");
        goto done;
    }
    ok = true;
    goto done;
memory:
    pp_store_error(error, PP_ERROR_MEMORY, "Cannot construct package manifest");
done:
    yyjson_mut_doc_free(doc);
    return ok;
}
#undef ADD

static const char *string_value(yyjson_val *value) {
    const char *s = yyjson_get_str(value);
    return s && strlen(s) == yyjson_get_len(value) ? s : NULL;
}
static bool valid_faces_metadata(yyjson_val *faces) {
    if (!yyjson_is_arr(faces) || !yyjson_arr_size(faces) || yyjson_arr_size(faces) > 4096)
        return false;
    size_t i, count;
    yyjson_val *face;
    yyjson_arr_foreach(faces, i, count, face) {
        const char *embedding = string_value(yyjson_obj_get(face, "embedding"));
        if (!yyjson_is_obj(face) || yyjson_obj_size(face) != 3 || !embedding ||
            !yyjson_is_bool(yyjson_obj_get(face, "no_subsetting")) ||
            !yyjson_is_bool(yyjson_obj_get(face, "bitmap_only")))
            return false;
        size_t kind;
        for (kind = 0; kind < 4; kind++)
            if (!strcmp(embedding, embedding_names[kind]))
                break;
        if (kind == 4)
            return false;
    }
    return true;
}
bool pp_manifest_check_font(const PPStoredResource *resource, PPAssets *assets, PPError *error) {
    if (resource->kind != PP_ASSET_FONT)
        return true;
    PPAsset *asset = pp_asset_retain(assets, resource->id);
    PPBytes bytes = pp_asset_source(asset);
    PPFontInfo info;
    bool ok = asset && pp_font_inspect(bytes, 0, &info, error);
    if (ok)
        ok = yyjson_arr_size(resource->faces) == info.face_count;
    for (size_t i = 0; ok && i < yyjson_arr_size(resource->faces); i++) {
        yyjson_val *face = yyjson_arr_get(resource->faces, i);
        ok = pp_font_inspect(bytes, (int)i, &info, error) && info.embeddable;
        if (ok)
            ok = !strcmp(string_value(yyjson_obj_get(face, "embedding")),
                         embedding_names[info.embedding]) &&
                 yyjson_get_bool(yyjson_obj_get(face, "no_subsetting")) == info.no_subsetting &&
                 yyjson_get_bool(yyjson_obj_get(face, "bitmap_only")) == info.bitmap_only;
    }
    pp_asset_release(asset);
    return ok || pp_store_error(error, PP_ERROR_FORMAT,
                                "Font embedding metadata or permission differs from actual bytes");
}
static bool safe_tree(yyjson_val *value, unsigned depth) {
    if (depth > 16)
        return false;
    if (yyjson_is_str(value))
        return string_value(value) != NULL;
    if (yyjson_is_arr(value)) {
        if (yyjson_arr_size(value) > 4096)
            return false;
        size_t i, n;
        yyjson_val *child;
        yyjson_arr_foreach(value, i, n, child) {
            if (!safe_tree(child, depth + 1))
                return false;
        }
    } else if (yyjson_is_obj(value)) {
        if (yyjson_obj_size(value) > 16)
            return false;
        size_t i, n;
        yyjson_val *key, *child;
        yyjson_obj_foreach(value, i, n, key, child) {
            if (!string_value(key) || !safe_tree(child, depth + 1))
                return false;
            size_t j, m;
            yyjson_val *other, *ignored;
            yyjson_obj_foreach(value, j, m, other, ignored) {
                if (j < i && !strcmp(yyjson_get_str(key), yyjson_get_str(other)))
                    return false;
            }
        }
    }
    return true;
}
bool pp_manifest_read(PPBytes bytes, PPManifest *manifest, PPError *error) {
    if (!bytes.size || bytes.size > PP_STORE_MANIFEST)
        return pp_store_error(error, PP_ERROR_RESOURCE_LIMIT, "Manifest exceeds 2 MiB");
    manifest->doc = yyjson_read((const char *)bytes.data, bytes.size, 0);
    yyjson_val *root = yyjson_doc_get_root(manifest->doc);
    if (!root || !safe_tree(root, 0) || !yyjson_is_obj(root) || yyjson_obj_size(root) != 5 ||
        !yyjson_is_uint(yyjson_obj_get(root, "schema")) ||
        yyjson_get_uint(yyjson_obj_get(root, "schema")) != 1)
        return pp_store_error(error, PP_ERROR_FORMAT,
                              "Invalid manifest or unsupported schema version");
    const char *name = string_value(yyjson_obj_get(root, "name"));
    if (!name || !*name || strlen(name) > PP_PROJECT_NAME_MAX)
        return pp_store_error(error, PP_ERROR_FORMAT, "Project name must contain 1–255 UTF-8 bytes");
    yyjson_val *resources = yyjson_obj_get(root, "resources");
    if (!yyjson_is_arr(resources) || yyjson_arr_size(resources) > PP_STORE_ENTRIES - 1)
        return pp_store_error(error, PP_ERROR_FORMAT, "Invalid manifest resources");
    manifest->count = yyjson_arr_size(resources);
    manifest->resources =
        calloc(manifest->count ? manifest->count : 1, sizeof(*manifest->resources));
    if (!manifest->resources) {
        manifest->count = 0;
        return pp_store_error(error, PP_ERROR_MEMORY, "Cannot allocate resource metadata");
    }
    for (size_t i = 0; i < manifest->count; i++) {
        yyjson_val *value = yyjson_arr_get(resources, i);
        PPStoredResource *r = &manifest->resources[i];
        const char *kind = string_value(yyjson_obj_get(value, "kind"));
        r->notice = string_value(yyjson_obj_get(value, "notice"));
        if (!yyjson_is_obj(value) ||
            !pp_store_id(string_value(yyjson_obj_get(value, "sha256")), &r->id) || !kind ||
            !r->notice)
            goto invalid;
        int k;
        for (k = 0; k < 4; k++)
            if (!strcmp(kind, kind_names[k]))
                break;
        if (k == 4)
            goto invalid;
        r->kind = (PPAssetKind)k;
        if (yyjson_obj_size(value) != (r->kind == PP_ASSET_FONT ? 4 : 3))
            goto invalid;
        if (r->kind == PP_ASSET_FONT) {
            r->faces = yyjson_obj_get(value, "faces");
            if (!valid_faces_metadata(r->faces))
                goto invalid;
        }
        for (size_t j = 0; j < i; j++)
            if (!memcmp(&r->id, &manifest->resources[j].id, sizeof(r->id)))
                goto invalid;
    }
    return true;
invalid:
    return pp_store_error(error, PP_ERROR_FORMAT, "Invalid or duplicate resource metadata");
}
static bool number(yyjson_val *object, const char *key, double *out) {
    yyjson_val *v = yyjson_obj_get(object, key);
    if (!yyjson_is_num(v))
        return false;
    *out = yyjson_get_num(v);
    return isfinite(*out);
}
static bool reference(PPManifest *manifest, yyjson_val *value, PPAssetId *out, PPAssetKind kind,
                      bool *used) {
    if (!pp_store_id(string_value(value), out))
        return false;
    for (size_t i = 0; i < manifest->count; i++)
        if (!memcmp(out, &manifest->resources[i].id, sizeof(*out))) {
            if (kind == PP_ASSET_FONT ? manifest->resources[i].kind != kind
                                      : manifest->resources[i].kind == PP_ASSET_FONT)
                return false;
            used[i] = true;
            return true;
        }
    return false;
}
static bool font_reference(PPManifest *manifest, yyjson_val *font, PPAssetId *id, int *face,
                           bool *used) {
    if (yyjson_is_null(font))
        return true;
    yyjson_val *v = yyjson_obj_get(font, "face");
    if (!yyjson_is_obj(font) || yyjson_obj_size(font) != 2 || !yyjson_is_uint(v) ||
        yyjson_get_uint(v) > INT_MAX ||
        !reference(manifest, yyjson_obj_get(font, "resource"), id, PP_ASSET_FONT, used))
        return false;
    *face = (int)yyjson_get_uint(v);
    return true;
}
bool pp_manifest_project(PPManifest *manifest, PPAssets *assets, PPProject **out, PPError *error) {
    *out = NULL;
    PPProject *project = NULL;
    bool ok = false;
    size_t item_count = 0;
    bool *used = calloc(manifest->count ? manifest->count : 1, sizeof(*used));
    if (!used)
        return pp_store_error(error, PP_ERROR_MEMORY, "Cannot allocate resource reference map");
    if (!pp_project_create(assets, &project, error)) {
        free(used);
        return false;
    }
    yyjson_val *root = yyjson_doc_get_root(manifest->doc),
               *settings = yyjson_obj_get(root, "settings"), *lists = yyjson_obj_get(root, "lists");
    PPProjectSettings s;
    if (!pp_project_set_name(project, string_value(yyjson_obj_get(root, "name")), error))
        goto done;
    if (!yyjson_is_obj(settings) || yyjson_obj_size(settings) != 4 ||
        !number(settings, "activation_dip", &s.activation_dip) ||
        !number(settings, "rotation_step_deg", &s.rotation_step_deg) ||
        !number(settings, "resize_factor", &s.resize_factor) ||
        !number(settings, "easing_seconds", &s.easing_seconds) || !yyjson_is_arr(lists) ||
        yyjson_arr_size(lists) != 8)
        goto invalid;
    if (!pp_project_set_settings(project, &s, error))
        goto done;
    for (int a = 0; a < PP_ANCHOR_COUNT; a++) {
        yyjson_val *list = yyjson_arr_get(lists, a), *items = yyjson_obj_get(list, "items"),
                   *enabled = yyjson_obj_get(list, "enabled"),
                   *def = yyjson_obj_get(list, "default");
        if (!yyjson_is_obj(list) || yyjson_obj_size(list) != 3 || !yyjson_is_arr(items) ||
            !yyjson_is_bool(enabled) || !yyjson_is_uint(def))
            goto invalid;
        size_t i, n;
        yyjson_val *value;
        yyjson_arr_foreach(items, i, n, value) {
            PPItem item = {0};
            const char *kind = string_value(yyjson_obj_get(value, "kind"));
            yyjson_val *id = yyjson_obj_get(value, "id");
            if (++item_count > 4096 || !kind || !yyjson_is_uint(id) ||
                !(item.id = yyjson_get_uint(id)) || !number(value, "angle", &item.angle_deg) ||
                !number(value, "opacity", &item.opacity))
                goto invalid;
            if (!strcmp(kind, "image")) {
                item.kind = PP_ITEM_IMAGE;
                if (yyjson_obj_size(value) != 6 ||
                    !number(value, "image_longest_dip", &item.image_longest_dip) ||
                    !reference(manifest, yyjson_obj_get(value, "resource"), &item.image_asset,
                               PP_ASSET_PNG, used))
                    goto invalid;
            } else if (!strcmp(kind, "text")) {
                item.kind = PP_ITEM_TEXT;
                PPTextStyle *style = &item.text_style;
                item.text = string_value(yyjson_obj_get(value, "text"));
                style->requested_family = string_value(yyjson_obj_get(value, "requested_family"));
                yyjson_val *color = yyjson_obj_get(value, "color");
                if (yyjson_obj_size(value) != 10 || !item.text || !style->requested_family ||
                    !number(value, "font_size_dip", &style->font_size_dip) ||
                    !yyjson_is_arr(color) || yyjson_arr_size(color) != 4 ||
                    !font_reference(manifest, yyjson_obj_get(value, "primary"),
                                    &style->primary_asset, &style->primary_face, used) ||
                    !font_reference(manifest, yyjson_obj_get(value, "fallback"),
                                    &style->fallback_asset, &style->fallback_face, used))
                    goto invalid;
                for (int c = 0; c < 4; c++) {
                    yyjson_val *v = yyjson_arr_get(color, c);
                    if (!yyjson_is_uint(v) || yyjson_get_uint(v) > 255)
                        goto invalid;
                    style->color[c] = (uint8_t)yyjson_get_uint(v);
                }
                if (!portable_text(assets, &item, error))
                    goto done;
            } else
                goto invalid;
            if (!pp_project_add_item(project, (PPAnchor)a, &item, NULL, error))
                goto done;
        }
        if (!pp_project_set_enabled(project, (PPAnchor)a, yyjson_get_bool(enabled), error))
            goto done;
        uint64_t default_id = yyjson_get_uint(def);
        if (n) {
            if (!default_id || !pp_project_set_default(project, (PPAnchor)a, default_id, error))
                goto invalid;
        } else if (default_id)
            goto invalid;
    }
    for (size_t i = 0; i < manifest->count; i++) {
        if (!used[i])
            goto invalid;
        if (!pp_project_set_notice(project, manifest->resources[i].id,
                                   manifest->resources[i].notice, error))
            goto done;
    }
    if (!pp_project_validate(project, error))
        goto done;
    *out = project;
    project = NULL;
    ok = true;
    goto done;
invalid:
    pp_store_error(error, PP_ERROR_FORMAT, "Invalid manifest item, reference, list, or settings");
done:
    free(used);
    pp_project_destroy(project);
    return ok;
}
