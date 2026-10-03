#include "store_internal.h"
#include "pointy/sha256.h"
#include <stdlib.h>
#include <string.h>

bool pp_store_error(PPError *error, int code, const char *message) {
    if (error) {
        error->code = code;
        snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return false;
}
bool pp_project_save_with_hooks(const PPProject *project, const char *path,
                                const PPProjectStoreHooks *hooks, PPError *error) {
    if (error)
        memset(error, 0, sizeof(*error));
    if (!project || !path || !*path)
        return pp_store_error(error, PP_ERROR_ARGUMENT, "Missing project or package path");
    PPManifest manifest = {0};
    char *json = NULL, *temporary = NULL;
    size_t size = 0;
    FILE *file = NULL;
    bool ok = false;
    if (!pp_manifest_write(project, &json, &size, &manifest, error))
        goto done;
    file = pp_platform_temporary_file(path, &temporary, error);
    if (!file)
        goto done;
    if (!pp_archive_write(file, json, size, &manifest, hooks, error))
        goto done;
    if (hooks && hooks->before_flush && !hooks->before_flush(hooks->context, error))
        goto done;
    if (!pp_platform_flush_file(file, error))
        goto done;
    int closed = fclose(file);
    file = NULL;
    if (closed) {
        pp_store_error(error, PP_ERROR_PLATFORM, "Cannot close completed project file");
        goto done;
    }
    if (hooks && hooks->before_replace && !hooks->before_replace(hooks->context, error))
        goto done;
    if (!pp_platform_replace_file(temporary, path, error))
        goto done;
    ok = true;
done:
    if (file)
        fclose(file);
    if (!ok && temporary)
        pp_platform_remove_file(temporary, NULL);
    free(temporary);
    free(json);
    pp_manifest_destroy(&manifest);
    return ok;
}
bool pp_project_save(const PPProject *project, const char *path, PPError *error) {
    return pp_project_save_with_hooks(project, path, NULL, error);
}

bool pp_project_load(const char *path, PPAssets *assets, PPProject **out, PPError *error) {
    if (out)
        *out = NULL;
    if (error)
        memset(error, 0, sizeof(*error));
    if (!path || !*path || !assets || !out)
        return pp_store_error(error, PP_ERROR_ARGUMENT,
                              "Missing package path, assets, or candidate output");
    FILE *file = pp_platform_open_file(path, "rb", error);
    if (!file)
        return false;
    mz_zip_archive zip = {0};
    bool opened = false, ok = false;
    PPManifest manifest = {0};
    PPBlob bytes = {0};
    PPAssets *staging = NULL;
    PPProject *candidate = NULL;
    size_t borrowed_budget = 0;
    if (!pp_archive_open(file, &zip, error))
        goto done;
    opened = true;
    int index = mz_zip_reader_locate_file(&zip, "manifest.json", NULL, 0);
    if (index < 0 || !pp_archive_extract(&zip, (mz_uint)index, PP_STORE_MANIFEST, &bytes, error) ||
        !pp_manifest_read((PPBytes){bytes.data, bytes.size}, &manifest, error))
        goto done;
    pp_blob_destroy(&bytes);
    if (mz_zip_reader_get_num_files(&zip) != manifest.count + 1) {
        pp_store_error(error, PP_ERROR_FORMAT, "ZIP resources differ from manifest");
        goto done;
    }
    if (!pp_assets_create(&staging, error))
        goto done;
    /* Storage and all asset mutation are synchronous. Occupy the staging budget
     * with existing app retention, so the two managers cannot retain independent
     * 512 MiB allocations. These reservations represent existing bytes, not copies. */
    size_t occupied = pp_assets_raster_bytes(assets);
    while (borrowed_budget < occupied) {
        size_t chunk = occupied - borrowed_budget;
        if (chunk > 256 * 1024 * 1024)
            chunk = 256 * 1024 * 1024;
        if (!pp_assets_reserve_raster(staging, chunk, error))
            goto done;
        borrowed_budget += chunk;
    }
    for (size_t i = 0; i < manifest.count; i++) {
        PPStoredResource *r = &manifest.resources[i];
        char name[75] = "resources/";
        pp_store_hex(r->id, name + 10);
        index = mz_zip_reader_locate_file(&zip, name, NULL, 0);
        if (index < 0) {
            pp_store_error(error, PP_ERROR_FORMAT, "Missing manifest resource");
            goto done;
        }
        if (!pp_archive_extract(&zip, (mz_uint)index, PP_STORE_RESOURCE, &bytes, error))
            goto done;
        PPAssetId actual;
        pp_sha256(bytes.data, bytes.size, actual.bytes);
        if (memcmp(&r->id, &actual, sizeof(actual))) {
            pp_store_error(error, PP_ERROR_FORMAT, "Resource SHA-256 mismatch");
            goto done;
        }
        if (!pp_asset_import(staging, (PPBytes){bytes.data, bytes.size}, r->kind, &actual, error))
            goto done;
        pp_blob_destroy(&bytes);
    }
    if (!pp_manifest_project(&manifest, staging, &candidate, error))
        goto done;
    pp_project_destroy(candidate);
    candidate = NULL;
    pp_assets_release_raster(staging, borrowed_budget);
    borrowed_budget = 0;
    pp_assets_trim_cache(staging);
    /* Sources remain owned by staging through transfer. No staging snapshots or
     * decoded caches overlap app allocations. Late transfer failures may leave
     * zero-retain imported sources in assets until manager destruction, per the
     * asset contract; the active project and its snapshots are never changed. */
    for (size_t i = 0; i < manifest.count; i++) {
        PPStoredResource *r = &manifest.resources[i];
        PPAsset *source = pp_asset_retain(staging, r->id);
        PPAssetId actual;
        bool imported =
            source && pp_asset_import(assets, pp_asset_source(source), r->kind, &actual, error);
        pp_asset_release(source);
        if (!imported)
            goto done;
    }
    if (!pp_manifest_project(&manifest, assets, &candidate, error))
        goto done;
    *out = candidate;
    candidate = NULL;
    ok = true;
done:
    pp_project_destroy(candidate);
    if (staging && borrowed_budget)
        pp_assets_release_raster(staging, borrowed_budget);
    pp_assets_destroy(staging);
    pp_blob_destroy(&bytes);
    pp_manifest_destroy(&manifest);
    if (opened)
        mz_zip_reader_end(&zip);
    fclose(file);
    return ok;
}
