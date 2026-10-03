#include "helpers.h"
static const char *empty;
static void test_archive_validation(void) {
    char future[1024];
    strcpy(future, empty);
    *strstr(future, "1") = '2';
    write_zip(future, NULL, (PPBytes){0}, false);
    reject_load();
    write_zip(empty, NULL, (PPBytes){0}, true);
    reject_load();
    const char *paths[] = {"../escape", "/absolute", "C:/absolute", "resources/../escape",
                           "resources\\escape"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(*paths); i++) {
        write_zip(empty, paths[i], (PPBytes){(const uint8_t *)"x", 1}, false);
        reject_load();
    }
    write_zip(empty, NULL, (PPBytes){0}, false);
    PPBlob b = read_bytes(package);
    size_t cd = 0;
    for (size_t i = 0; i + 46 < b.size; i++)
        if (!memcmp(b.data + i, "PK\1\2", 4)) {
            cd = i;
            break;
        }
    CHECK(cd);
    b.data[cd + 5] = 3;
    b.data[cd + 38] = 0;
    b.data[cd + 39] = 0;
    b.data[cd + 40] = 0xff;
    b.data[cd + 41] = 0xa1;
    write_bytes(package, b.data, b.size);
    reject_load();
    b.data[cd + 40] = 0;
    b.data[cd + 41] = 0;
    memset(b.data + cd + 24, 0xff, 4);
    write_bytes(package, b.data, b.size);
    reject_load();
    pp_blob_destroy(&b);
}
static void test_manifest_rejections(void);
static void test_restricted_collection_sibling(void);
static void test_declared_limits(void);
static void test_archive_trailer_agreement(void);
int main(void) {
    begin_test();
    PPBlob fixture = read_bytes(PP_PROJECT_FIXTURES "/empty-v1.json");
    empty = (const char *)fixture.data;
    test_archive_validation();
    test_manifest_rejections();
    test_restricted_collection_sibling();
    test_declared_limits();
    test_archive_trailer_agreement();
    pp_blob_destroy(&fixture);
    end_test();
    puts("archive rejection cases passed");
    return 0;
}

static char *resource_manifest(PPBytes bytes, const char *kind, bool text, int face,
                               PPAssetId *id) {
    pp_sha256(bytes.data, bytes.size, id->bytes);
    char hash[65];
    hex_id(*id, hash);
    yyjson_doc *doc = yyjson_read(empty, strlen(empty), 0);
    CHECK(doc);
    yyjson_mut_doc *mut = yyjson_doc_mut_copy(doc, NULL);
    yyjson_doc_free(doc);
    CHECK(mut);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(mut),
                   *resources = yyjson_mut_obj_get(root, "resources");
    yyjson_mut_val *resource = yyjson_mut_obj(mut);
    CHECK(yyjson_mut_arr_append(resources, resource));
    CHECK(yyjson_mut_obj_add_strcpy(mut, resource, "sha256", hash));
    CHECK(yyjson_mut_obj_add_str(mut, resource, "kind", kind));
    CHECK(yyjson_mut_obj_add_str(mut, resource, "notice", "fixture license"));
    char item[1024];
    if (text)
        snprintf(item, sizeof(item),
                 "{\"id\":1,\"kind\":\"text\",\"angle\":0,\"opacity\":1,\"text\":\"A\",\"font_size_"
                 "dip\":32,\"requested_family\":\"Pointy "
                 "Fixture\",\"color\":[0,0,0,255],\"primary\":{\"resource\":\"%s\",\"face\":%d},"
                 "\"fallback\":null}",
                 hash, face);
    else
        snprintf(item, sizeof(item),
                 "{\"id\":1,\"kind\":\"image\",\"angle\":0,\"opacity\":1,\"image_longest_dip\":64,"
                 "\"resource\":\"%s\"}",
                 hash);
    yyjson_doc *itemdoc = yyjson_read(item, strlen(item), 0);
    CHECK(itemdoc);
    yyjson_mut_val *list = yyjson_mut_arr_get(yyjson_mut_obj_get(root, "lists"), 0);
    CHECK(yyjson_mut_obj_remove_key(list, "default"));
    CHECK(yyjson_mut_obj_add_uint(mut, list, "default", 1));
    CHECK(yyjson_mut_arr_append(yyjson_mut_obj_get(list, "items"),
                                yyjson_val_mut_copy(mut, yyjson_doc_get_root(itemdoc))));
    yyjson_doc_free(itemdoc);
    char *result = yyjson_mut_write(mut, 0, NULL);
    CHECK(result);
    yyjson_mut_doc_free(mut);
    return result;
}
static void test_manifest_rejections(void) {
    const char *svg = "<svg width='20' height='20'><rect width='20' height='20' fill='red'/></svg>";
    PPBytes source = {(const uint8_t *)svg, strlen(svg)};
    PPAssetId id;
    char name[80], hash[65];
    char *json = resource_manifest(source, "svg", false, 0, &id);
    hex_id(id, hash);
    snprintf(name, sizeof(name), "resources/%s", hash);
    write_zip(json, NULL, (PPBytes){0}, false);
    reject_load();
    write_zip(json, name, (PPBytes){(const uint8_t *)"corrupt", 7}, false);
    reject_load();
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    CHECK(doc);
    yyjson_mut_doc *mut = yyjson_doc_mut_copy(doc, NULL);
    yyjson_doc_free(doc);
    yyjson_mut_val *items = yyjson_mut_obj_get(
        yyjson_mut_arr_get(yyjson_mut_obj_get(yyjson_mut_doc_get_root(mut), "lists"), 0), "items");
    CHECK(yyjson_mut_arr_append(items, yyjson_mut_val_mut_copy(mut, yyjson_mut_arr_get(items, 0))));
    char *duplicate = yyjson_mut_write(mut, 0, NULL);
    write_zip(duplicate, name, source, false);
    reject_load();
    free(duplicate);
    yyjson_mut_doc_free(mut);
    free(json);
    PPBlob font = read_bytes(PP_FONT_FIXTURES "/PointyFixture.ttf");
    json = resource_manifest((PPBytes){font.data, font.size}, "font", true, 1000, &id);
    hex_id(id, hash);
    snprintf(name, sizeof(name), "resources/%s", hash);
    write_zip(json, name, (PPBytes){font.data, font.size}, false);
    reject_load();
    free(json);
    /* Change the real OS/2 embedding flags, then recompute the digest. The
       manifest has no authority to turn restricted bytes into permitted bytes. */
    unsigned tables = (font.data[4] << 8) | font.data[5];
    bool patched = false;
    for (unsigned i = 0; i < tables; i++) {
        uint8_t *t = font.data + 12 + 16 * i;
        if (!memcmp(t, "OS/2", 4)) {
            size_t off = ((size_t)t[8] << 24) | ((size_t)t[9] << 16) | (t[10] << 8) | t[11];
            CHECK(off + 10 < font.size);
            font.data[off + 8] = 0;
            font.data[off + 9] = 2;
            patched = true;
            break;
        }
    }
    CHECK(patched);
    json = resource_manifest((PPBytes){font.data, font.size}, "font", true, 0, &id);
    hex_id(id, hash);
    snprintf(name, sizeof(name), "resources/%s", hash);
    write_zip(json, name, (PPBytes){font.data, font.size}, false);
    reject_load();
    free(json);
    pp_blob_destroy(&font);
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void put_be32(uint8_t *p, uint32_t n) {
    p[0] = (uint8_t)(n >> 24);
    p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
}
static void test_restricted_collection_sibling(void) {
    PPBlob font = read_bytes(PP_FONT_FIXTURES "/PointyFixture.ttc");
    CHECK(!memcmp(font.data, "ttcf", 4));
    size_t face = be32(font.data + 16), record = 0;
    CHECK(face + 12 < font.size);
    unsigned tables = (font.data[face + 4] << 8) | font.data[face + 5];
    for (unsigned i = 0; i < tables; i++)
        if (!memcmp(font.data + face + 12 + 16 * i, "OS/2", 4)) {
            record = face + 12 + 16 * i;
            break;
        }
    CHECK(record);
    size_t source = be32(font.data + record + 8), length = be32(font.data + record + 12);
    CHECK(source + length <= font.size && length >= 10);
    /* Give the second face its own OS/2 table even when the fixture collection
       deduplicates that table. The permitted selected face remains unchanged. */
    size_t old_size = font.size;
    font.data = realloc(font.data, font.size + length);
    CHECK(font.data);
    memcpy(font.data + old_size, font.data + source, length);
    put_be32(font.data + record + 8, (uint32_t)old_size);
    font.data[old_size + 8] = 0;
    font.data[old_size + 9] = 2;
    font.size += length;
    PPFontInfo selected, sibling;
    CHECK(pp_font_inspect((PPBytes){font.data, font.size}, 0, &selected, &error) &&
          selected.embeddable);
    CHECK(pp_font_inspect((PPBytes){font.data, font.size}, 1, &sibling, &error) &&
          !sibling.embeddable);
    PPAssetId id;
    char *json = resource_manifest((PPBytes){font.data, font.size}, "font", true, 0, &id), hash[65],
         name[75];
    hex_id(id, hash);
    snprintf(name, sizeof(name), "resources/%s", hash);
    write_zip(json, name, (PPBytes){font.data, font.size}, false);
    reject_load();
    free(json);
    /* Saving the permitted face must also reject packaging its sibling bytes. */
    PPAssets *assets = NULL;
    PPProject *project = NULL;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_create(assets, &project, &error));
    CHECK(pp_project_save(project, package, &error));
    uint8_t before[32], after[32];
    digest_file(package, before);
    PPItem item = {0};
    item.kind = PP_ITEM_TEXT;
    item.text = "A";
    item.opacity = 1;
    item.text_style.font_size_dip = 32;
    item.text_style.color[3] = 255;
    item.text_style.requested_family = "Pointy Fixture";
    CHECK(pp_asset_import(assets, (PPBytes){font.data, font.size}, PP_ASSET_FONT,
                          &item.text_style.primary_asset, &error));
    CHECK(pp_project_add_item(project, PP_ANCHOR_TOP_LEFT, &item, NULL, &error));
    uint64_t revision = pp_project_revision(project);
    CHECK(!pp_project_save(project, package, &error));
    digest_file(package, after);
    CHECK(!memcmp(before, after, 32));
    CHECK(pp_project_revision(project) == revision);
    PPProject *reopened = NULL;
    CHECK(pp_project_load(package, assets, &reopened, &error));
    CHECK(pp_project_anchor(reopened, PP_ANCHOR_TOP_LEFT)->count == 0);
    pp_project_destroy(reopened);
    pp_project_destroy(project);
    pp_assets_destroy(assets);
    pp_blob_destroy(&font);
}
static void put_le32(uint8_t *p, uint32_t n) {
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
    p[2] = (uint8_t)(n >> 16);
    p[3] = (uint8_t)(n >> 24);
}
static void test_declared_limits(void) {
    for (int variant = 0; variant < 4; variant++) {
        write_zip(empty, NULL, (PPBytes){0}, false);
        PPBlob bytes = read_bytes(package);
        size_t cd = 0;
        for (size_t p = 0; p + 46 < bytes.size; p++)
            if (!memcmp(bytes.data + p, "PK\1\2", 4)) {
                cd = p;
                break;
            }
        CHECK(cd);
        if (variant == 0)
            put_le32(bytes.data + cd + 24, 2 * 1024 * 1024 + 1);
        if (variant == 1) {
            bytes.data[bytes.size - 14] = 1;
            bytes.data[bytes.size - 13] = 16;
            bytes.data[bytes.size - 12] = 1;
            bytes.data[bytes.size - 11] = 16;
        }
        if (variant == 2) {
            bytes.data[cd + 28] = 1;
            bytes.data[cd + 29] = 4;
        }
        if (variant == 3)
            put_le32(bytes.data + cd + 24, UINT32_MAX);
        write_bytes(package, bytes.data, bytes.size);
        pp_blob_destroy(&bytes);
        reject_load();
        CHECK(error.code == PP_ERROR_RESOURCE_LIMIT);
    }
    /* Nine individually legal 64 MiB declarations exceed the total. No large
       payload is needed: the directory must reject before any extraction. */
    mz_zip_archive zip = {0};
    CHECK(mz_zip_writer_init_heap(&zip, 0, 0));
    CHECK(mz_zip_writer_add_mem(&zip, "manifest.json", empty, strlen(empty), 0));
    for (unsigned i = 0; i < 9; i++) {
        char name[75];
        snprintf(name, sizeof(name), "resources/%064x", i);
        CHECK(mz_zip_writer_add_mem(&zip, name, "x", 1, 0));
    }
    void *data = NULL;
    size_t size = 0;
    CHECK(mz_zip_writer_finalize_heap_archive(&zip, &data, &size));
    CHECK(mz_zip_writer_end(&zip));
    for (size_t p = 0; p + 46 < size; p++) {
        uint8_t *entry = (uint8_t *)data + p;
        if (!memcmp(entry, "PK\1\2", 4) && !memcmp(entry + 46, "resources/", 10))
            put_le32(entry + 24, 64 * 1024 * 1024);
    }
    write_bytes(package, data, size);
    mz_free(data);
    reject_load();
    CHECK(error.code == PP_ERROR_RESOURCE_LIMIT);
    char name[75] = "resources/0000000000000000000000000000000000000000000000000000000000000000";
    write_zip(empty, name, (PPBytes){(const uint8_t *)"x", 1}, false);
    PPBlob bytes = read_bytes(package);
    for (size_t p = 0; p + 46 < bytes.size; p++)
        if (!memcmp(bytes.data + p, "PK\1\2", 4) && !memcmp(bytes.data + p + 46, "resources/", 10))
            put_le32(bytes.data + p + 24, 64 * 1024 * 1024 + 1);
    write_bytes(package, bytes.data, bytes.size);
    pp_blob_destroy(&bytes);
    reject_load();
    CHECK(error.code == PP_ERROR_RESOURCE_LIMIT);
}
static uint32_t le32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void test_archive_trailer_agreement(void) {
    write_zip(empty, NULL, (PPBytes){0}, false);
    PPBlob bytes = read_bytes(package);
    size_t end = bytes.size - 22;
    uint32_t cd = le32(bytes.data + end + 16), cd_size = le32(bytes.data + end + 12);
    uint8_t *extended = calloc(1, bytes.size + 76);
    CHECK(extended);
    memcpy(extended, bytes.data, end);
    memcpy(extended + end + 76, bytes.data + end, 22);
    /* A ZIP64 trailer concealed inside the final central entry's comment must
       not let the underlying parser reinterpret preflight-approved metadata. */
    extended[cd + 32] = 76;
    put_le32(extended + end, 0x06064b50);
    put_le32(extended + end + 4, 44);
    extended[end + 12] = 45;
    extended[end + 14] = 45;
    put_le32(extended + end + 24, 1);
    put_le32(extended + end + 32, 1);
    put_le32(extended + end + 40, cd_size + 76);
    put_le32(extended + end + 48, cd);
    put_le32(extended + end + 56, 0x07064b50);
    put_le32(extended + end + 64, (uint32_t)end);
    put_le32(extended + end + 72, 1);
    put_le32(extended + end + 76 + 12, cd_size + 76);
    write_bytes(package, extended, bytes.size + 76);
    reject_load();
    free(extended);
    /* A valid global ZIP comment may contain a false EOCD signature. Both
       parsers must use the real end record whose comment reaches file end. */
    extended = calloc(1, bytes.size + 22);
    CHECK(extended);
    memcpy(extended, bytes.data, bytes.size);
    extended[end + 20] = 22;
    put_le32(extended + bytes.size, 0x06054b50);
    extended[bytes.size + 20] = 1;
    write_bytes(package, extended, bytes.size + 22);
    PPAssets *assets = NULL;
    PPProject *project = NULL;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_load(package, assets, &project, &error));
    CHECK(pp_project_anchor(project, PP_ANCHOR_TOP_LEFT)->count == 0);
    pp_project_destroy(project);
    pp_assets_destroy(assets);
    free(extended);
    pp_blob_destroy(&bytes);
}
