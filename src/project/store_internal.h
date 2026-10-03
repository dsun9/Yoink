#ifndef POINTY_STORE_INTERNAL_H
#define POINTY_STORE_INTERNAL_H
#include "pointy/project_store.h"
#include "pointy/platform.h"
#include <miniz.h>
#include <yyjson.h>
#define PP_STORE_COMPRESSED ((uint64_t)256 * 1024 * 1024)
#define PP_STORE_TOTAL ((uint64_t)512 * 1024 * 1024)
#define PP_STORE_RESOURCE ((size_t)64 * 1024 * 1024)
#define PP_STORE_MANIFEST ((size_t)2 * 1024 * 1024)
#define PP_STORE_ENTRIES 4096
#define PP_STORE_NAME 1024

typedef struct PPStoredResource {
    PPAssetId id;
    PPAssetKind kind;
    PPAsset *asset;     /* retained only when collecting a save */
    const char *notice; /* borrowed from project or manifest document */
} PPStoredResource;
typedef struct PPManifest {
    yyjson_doc *doc;
    PPStoredResource *resources;
    size_t count;
} PPManifest;
bool pp_store_error(PPError *error, int code, const char *message);
void pp_store_hex(PPAssetId id, char out[65]);
bool pp_store_id(const char *text, PPAssetId *out);
bool pp_manifest_write(const PPProject *project, char **json, size_t *size, PPManifest *manifest,
                       PPError *error);
bool pp_manifest_read(PPBytes bytes, PPManifest *manifest, PPError *error);
bool pp_manifest_project(PPManifest *manifest, PPAssets *assets, PPProject **out, PPError *error);
void pp_manifest_destroy(PPManifest *manifest);
bool pp_archive_open(FILE *file, mz_zip_archive *zip, PPError *error);
bool pp_archive_extract(mz_zip_archive *zip, mz_uint index, size_t limit, PPBlob *out,
                        PPError *error);
bool pp_archive_write(FILE *file, const char *json, size_t size, const PPManifest *manifest,
                      const PPProjectStoreHooks *hooks, PPError *error);
#endif
