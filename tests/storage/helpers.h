#ifndef POINTY_STORAGE_TEST_HELPERS_H
#define POINTY_STORAGE_TEST_HELPERS_H
#include "pointy/project_store.h"
#include "pointy/platform.h"
#include "pointy/sha256.h"
#include <miniz.h>
#include <yyjson.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static PPError error;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error.message);            \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static char directory[MAX_PATH], package[MAX_PATH];
static void begin_test(void) {
    snprintf(directory, sizeof(directory), "pointy-storage-%lu-%llu", GetCurrentProcessId(),
             GetTickCount64());
    CHECK(CreateDirectoryA(directory, NULL));
    snprintf(package, sizeof(package), "%s/\xe6\x89\x8b-\xc3\xa9.pointy", directory);
}
static void end_test(void) {
    CHECK(pp_platform_remove_file(package, &error));
    CHECK(RemoveDirectoryA(directory));
}
static PPBlob read_bytes(const char *path) {
    FILE *f = pp_platform_open_file(path, "rb", &error);
    CHECK(f);
    CHECK(!_fseeki64(f, 0, SEEK_END));
    __int64 n = _ftelli64(f);
    CHECK(n >= 0);
    rewind(f);
    PPBlob b = {malloc((size_t)n + 1), (size_t)n};
    CHECK(b.data);
    CHECK(fread(b.data, 1, b.size, f) == b.size);
    b.data[b.size] = 0;
    CHECK(!fclose(f));
    return b;
}
static void write_bytes(const char *path, const void *data, size_t size) {
    FILE *f = pp_platform_open_file(path, "wb", &error);
    CHECK(f);
    CHECK(fwrite(data, 1, size, f) == size);
    CHECK(!fclose(f));
}
static void digest_file(const char *path, uint8_t hash[32]) {
    PPBlob b = read_bytes(path);
    pp_sha256(b.data, b.size, hash);
    pp_blob_destroy(&b);
}
static void hex_id(PPAssetId id, char text[65]) {
    for (int i = 0; i < 32; i++)
        snprintf(text + 2 * i, 3, "%02x", id.bytes[i]);
}
static void write_zip(const char *manifest, const char *name, PPBytes bytes, bool duplicate) {
    mz_zip_archive zip = {0};
    void *data = NULL;
    size_t size = 0;
    char placeholder[1025];
    if (name) {
        CHECK(strlen(name) < sizeof(placeholder));
        memset(placeholder, 'x', strlen(name));
        placeholder[strlen(name)] = 0;
    }
    CHECK(mz_zip_writer_init_heap(&zip, 0, 0));
    CHECK(mz_zip_writer_add_mem(&zip, "manifest.json", manifest, strlen(manifest), 0));
    if (name)
        CHECK(mz_zip_writer_add_mem(&zip, placeholder, bytes.data, bytes.size, 0));
    if (duplicate)
        CHECK(mz_zip_writer_add_mem(&zip, "manifest.json", manifest, strlen(manifest), 0));
    CHECK(mz_zip_writer_finalize_heap_archive(&zip, &data, &size));
    CHECK(mz_zip_writer_end(&zip));
    /* miniz refuses some unsafe names when writing. Mutate equal-length local
       and central names to exercise the reader, without using a disk path. */
    if (name)
        for (size_t i = 0; i + 46 < size; i++) {
            uint8_t *p = (uint8_t *)data + i;
            size_t start = 0;
            if (!memcmp(p, "PK\1\2", 4))
                start = 46;
            else if (!memcmp(p, "PK\3\4", 4))
                start = 30;
            if (start && i + start + strlen(name) <= size &&
                !memcmp(p + start, placeholder, strlen(name)))
                memcpy(p + start, name, strlen(name));
        }
    write_bytes(package, data, size);
    mz_free(data);
}
static void reject_load(void) {
    PPAssets *assets = NULL;
    PPProject *candidate = (PPProject *)1;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(!pp_project_load(package, assets, &candidate, &error));
    CHECK(!candidate);
    CHECK(error.code);
    CHECK(pp_assets_external_raster_bytes(assets) == 0);
    pp_assets_destroy(assets);
}
#endif
