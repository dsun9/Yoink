#include "helpers.h"
#include "pointy/scene.h"
static const char *empty;
static void test_archive_validation(void) {
    char future[1024];
    strcpy(future, empty);
    *strstr(future, "1") = '2';
    write_zip(future, NULL, (PPBytes){0}, false);
    reject_load();
    write_zip(empty, NULL, (PPBytes){0}, true);
    reject_load();
    const char *paths[] = {"../escape", "/absolute", "C:/absolute", "assets/../escape",
                           "fonts\\escape"};
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
static void test_named_instance_is_not_collection_face(void);
static void test_wrong_resource_namespace(void);
static void test_rotation_metadata(void);
static void test_project_names(void) {
    const char *names[]={NULL,""," \xc2\xa0","line\nname"};
    for (size_t i=0;i<sizeof(names)/sizeof(*names)+2;i++) {
        yyjson_doc *doc=yyjson_read(empty,strlen(empty),0);
        CHECK(doc);
        yyjson_mut_doc *mut=yyjson_doc_mut_copy(doc,NULL);
        yyjson_doc_free(doc);
        yyjson_mut_val *root=yyjson_mut_doc_get_root(mut);
        CHECK(yyjson_mut_obj_remove_key(root,"name"));
        char oversized[257]; memset(oversized,'a',256); oversized[256]=0;
        if (i==4) CHECK(yyjson_mut_obj_add_str(mut,root,"name",oversized));
        else if (i==5) CHECK(yyjson_mut_obj_add_strn(mut,root,"name","embedded\0nul",12));
        else if (names[i]) CHECK(yyjson_mut_obj_add_str(mut,root,"name",names[i]));
        char *json=yyjson_mut_write(mut,0,NULL); CHECK(json);
        write_zip(json,NULL,(PPBytes){0},false);
        reject_load();
        free(json); yyjson_mut_doc_free(mut);
    }
    char malformed[1024]; strcpy(malformed,empty);
    char *name=strstr(malformed,"Untitled project"); CHECK(name);
    name[0]=(char)0xc0; name[1]=(char)0xaf;
    write_zip(malformed,NULL,(PPBytes){0},false);
    reject_load();
}
int main(void) {
    begin_test();
    PPBlob fixture = read_bytes(PP_PROJECT_FIXTURES "/empty-v1.json");
    empty = (const char *)fixture.data;
    test_archive_validation();
    test_manifest_rejections();
    test_restricted_collection_sibling();
    test_declared_limits();
    test_archive_trailer_agreement();
    test_named_instance_is_not_collection_face();
    test_wrong_resource_namespace();
    test_project_names();
    test_rotation_metadata();
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
    if (!strcmp(kind, "font")) {
        PPFontInfo info;
        CHECK(pp_font_inspect(bytes, 0, &info, &error));
        size_t count = info.face_count;
        yyjson_mut_val *faces = yyjson_mut_arr(mut);
        CHECK(faces && yyjson_mut_obj_add_val(mut, resource, "faces", faces));
        const char *embedding[] = {"installable", "editable", "preview_only", "restricted"};
        for (size_t index = 0; index < count; index++) {
            CHECK(pp_font_inspect(bytes, (int)index, &info, &error));
            yyjson_mut_val *face = yyjson_mut_obj(mut);
            CHECK(face && yyjson_mut_arr_append(faces, face));
            CHECK(yyjson_mut_obj_add_str(mut, face, "embedding", embedding[info.embedding]));
            CHECK(yyjson_mut_obj_add_bool(mut, face, "no_subsetting", info.no_subsetting));
            CHECK(yyjson_mut_obj_add_bool(mut, face, "bitmap_only", info.bitmap_only));
        }
    }
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
static void test_rotation_metadata(void) {
    const char *svg = "<svg width='20' height='20'><rect width='20' height='20' fill='red'/></svg>";
    PPBytes source = {(const uint8_t *)svg, strlen(svg)};
    PPAssetId id;
    char *base = resource_manifest(source, "svg", false, 0, &id);
    char hash[65], name[80];
    hex_id(id, hash);
    snprintf(name, sizeof(name), "assets/%s", hash);
    for (int variant = 0; variant < 8; variant++) {
        yyjson_doc *read = yyjson_read(base, strlen(base), 0);
        CHECK(read);
        yyjson_mut_doc *doc = yyjson_doc_mut_copy(read, NULL);
        yyjson_doc_free(read);
        CHECK(doc);
        yyjson_mut_val *list = yyjson_mut_arr_get(
            yyjson_mut_obj_get(yyjson_mut_doc_get_root(doc), "lists"), 0);
        yyjson_mut_val *item = yyjson_mut_arr_get(yyjson_mut_obj_get(list, "items"), 0);
        if (variant >= 1 && variant <= 4)
            CHECK(yyjson_mut_obj_add_str(doc, item, "rotation_center",
                variant == 1 ? "sprite" : variant == 2 ? "mouse" : variant == 3 ? "center" : ""));
        else if (variant == 5)
            CHECK(yyjson_mut_obj_add_uint(doc, item, "rotation_center", 1));
        else if (variant == 6)
            CHECK(yyjson_mut_obj_add_null(doc, item, "rotation_center"));
        else if (variant == 7)
            CHECK(yyjson_mut_obj_add_bool(doc, item, "rotation_center", true));
        char *json = yyjson_mut_write(doc, 0, NULL);
        CHECK(json);
        write_zip(json, name, source, false);
        free(json); yyjson_mut_doc_free(doc);
        if (variant >= 3) reject_load();
        else {
            PPAssets *assets = NULL;
            PPProject *project = NULL;
            CHECK(pp_assets_create(&assets, &error));
            CHECK(pp_project_load(package, assets, &project, &error));
            CHECK(pp_project_target(project, PP_ANCHOR_TOP_LEFT)->rotation_center ==
                  (variant == 2 ? PP_ROTATION_MOUSE_POSITION : PP_ROTATION_SPRITE_CENTER));
            pp_project_destroy(project); pp_assets_destroy(assets);
        }
    }
    free(base);
    puts("PASS legacy and explicit rotation centers; unknown/non-string centers rejected.");
}
static char *changed_metadata(const void *bytes, size_t size, int variant, size_t *out_size) {
    yyjson_doc *source = yyjson_read(bytes, size, 0);
    CHECK(source);
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(source, NULL);
    yyjson_doc_free(source);
    CHECK(doc);
    yyjson_mut_val *resources = yyjson_mut_obj_get(yyjson_mut_doc_get_root(doc), "resources");
    bool changed = false;
    for (size_t i = 0; i < yyjson_mut_arr_size(resources); i++) {
        yyjson_mut_val *resource = yyjson_mut_arr_get(resources, i);
        const char *kind = yyjson_mut_get_str(yyjson_mut_obj_get(resource, "kind"));
        if (strcmp(kind, variant == 7 ? "svg" : "font"))
            continue;
        if (variant == 7)
            CHECK(yyjson_mut_obj_add_val(doc, resource, "faces", yyjson_mut_arr(doc)));
        else {
            yyjson_mut_val *faces = yyjson_mut_obj_get(resource, "faces"),
                           *face = yyjson_mut_arr_get(faces, 0);
            CHECK(face);
            if (variant == 6)
                CHECK(yyjson_mut_arr_append(faces, yyjson_mut_val_mut_copy(doc, face)));
            else {
                const char *key = variant == 3   ? "embedding"
                                  : variant == 4 ? "no_subsetting"
                                                 : "bitmap_only";
                bool old_flag = yyjson_mut_get_bool(yyjson_mut_obj_get(face, key));
                CHECK(yyjson_mut_obj_remove_key(face, key));
                if (variant == 3)
                    CHECK(yyjson_mut_obj_add_str(doc, face, key, "installable"));
                else
                    CHECK(yyjson_mut_obj_add_bool(doc, face, key, !old_flag));
            }
        }
        changed = true;
        break;
    }
    CHECK(changed);
    char *result = yyjson_mut_write(doc, 0, out_size);
    CHECK(result);
    yyjson_mut_doc_free(doc);
    return result;
}
static void test_manifest_rejections(void) {
    const char *svg = "<svg width='20' height='20'><rect width='20' height='20' fill='red'/></svg>";
    PPBytes source = {(const uint8_t *)svg, strlen(svg)};
    PPAssetId id;
    char name[80], hash[65];
    char *json = resource_manifest(source, "svg", false, 0, &id);
    hex_id(id, hash);
    snprintf(name, sizeof(name), "assets/%s", hash);
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
    snprintf(name, sizeof(name), "fonts/%s", hash);
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
    size_t forged_size;
    char *forged = changed_metadata(json, strlen(json), 3, &forged_size);
    free(json);
    json = forged; /* Restricted actual bytes falsely claim installable embedding. */
    hex_id(id, hash);
    snprintf(name, sizeof(name), "fonts/%s", hash);
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
    snprintf(name, sizeof(name), "fonts/%s", hash);
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
        snprintf(name, sizeof(name), "assets/%064x", i);
        CHECK(mz_zip_writer_add_mem(&zip, name, "x", 1, 0));
    }
    void *data = NULL;
    size_t size = 0;
    CHECK(mz_zip_writer_finalize_heap_archive(&zip, &data, &size));
    CHECK(mz_zip_writer_end(&zip));
    for (size_t p = 0; p + 46 < size; p++) {
        uint8_t *entry = (uint8_t *)data + p;
        if (!memcmp(entry, "PK\1\2", 4) && !memcmp(entry + 46, "assets/", 7))
            put_le32(entry + 24, 64 * 1024 * 1024);
    }
    write_bytes(package, data, size);
    mz_free(data);
    reject_load();
    CHECK(error.code == PP_ERROR_RESOURCE_LIMIT);
    char name[75] = "assets/0000000000000000000000000000000000000000000000000000000000000000";
    write_zip(empty, name, (PPBytes){(const uint8_t *)"x", 1}, false);
    PPBlob bytes = read_bytes(package);
    for (size_t p = 0; p + 46 < bytes.size; p++)
        if (!memcmp(bytes.data + p, "PK\1\2", 4) && !memcmp(bytes.data + p + 46, "assets/", 7))
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

static void test_named_instance_is_not_collection_face(void) {
    PPBlob font = read_bytes(PP_FONT_FIXTURES "/PointyVariable.ttf");
    PPFontInfo info;
    CHECK(pp_font_inspect((PPBytes){font.data, font.size}, 65536, &info, &error));
    CHECK(info.face_count == 1 && info.embeddable);
    PPAssets *assets = NULL;
    PPProject *active = NULL, *candidate = NULL;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_create(assets, &active, &error));
    PPItem item = {0};
    item.kind = PP_ITEM_TEXT;
    item.text = "A";
    item.opacity = 1;
    item.text_style.font_size_dip = 32;
    item.text_style.color[3] = 255;
    item.text_style.requested_family = "Pointy Fixture";
    CHECK(pp_asset_import(assets, (PPBytes){font.data, font.size}, PP_ASSET_FONT,
                          &item.text_style.primary_asset, &error));
    uint64_t id;
    CHECK(pp_project_add_item(active, PP_ANCHOR_TOP_LEFT, &item, &id, &error));
    char original[MAX_PATH];
    snprintf(original, sizeof(original), "%s/valid.pointy", directory);
    CHECK(pp_project_save(active, original, &error));
    uint8_t before[32], after[32];
    digest_file(original, before);
    PPScene *scene = NULL;
    uint64_t instance;
    CHECK(pp_scene_create(&scene, &error));
    CHECK(
        pp_scene_spawn(scene, active, PP_ANCHOR_TOP_LEFT, (PPVec2){30, 40}, 0, &instance, &error));
    PPItemSnapshot *snapshot = pp_scene_get(scene, instance)->snapshot;
    uint64_t revision = pp_project_revision(active);
    size_t retained = pp_assets_raster_bytes(assets);
    PPAssetId resource;
    char *json = resource_manifest((PPBytes){font.data, font.size}, "font", true, 65536, &resource);
    char hash[65], name[75];
    hex_id(resource, hash);
    snprintf(name, sizeof(name), "fonts/%s", hash);
    write_zip(json, name, (PPBytes){font.data, font.size}, false);
    free(json);
    CHECK(!pp_project_load(package, assets, &candidate, &error));
    CHECK(!candidate);
    CHECK(error.code == PP_ERROR_FORMAT);
    CHECK(pp_project_revision(active) == revision &&
          pp_project_item(active, id)->text_style.primary_face == 0);
    CHECK(pp_assets_raster_bytes(assets) == retained);
    CHECK(pp_scene_count(scene) == 1 && pp_scene_get(scene, instance)->snapshot == snapshot);
    CHECK(pp_scene_get(scene, instance)->pose.center.x == 30 &&
          pp_scene_get(scene, instance)->pose.center.y == 40);
    digest_file(original, after);
    CHECK(!memcmp(before, after, 32));
    CHECK(pp_project_load(original, assets, &candidate, &error));
    CHECK(pp_project_item(candidate, id)->text_style.primary_face == 0);
    pp_project_destroy(candidate);
    pp_scene_destroy(scene);
    pp_project_destroy(active);
    pp_assets_destroy(assets);
    pp_blob_destroy(&font);
    CHECK(pp_platform_remove_file(original, &error));
}

static void test_wrong_resource_namespace(void) {
    PPAssets *assets = NULL;
    PPProject *project = NULL;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_create(assets, &project, &error));
    const char *svg = "<svg width='20' height='20'><rect width='20' height='20' fill='red'/></svg>";
    PPItem image = {0};
    image.kind = PP_ITEM_IMAGE;
    image.opacity = 1;
    image.image_longest_dip = 20;
    CHECK(pp_asset_import(assets, (PPBytes){(const uint8_t *)svg, strlen(svg)}, PP_ASSET_SVG,
                          &image.image_asset, &error));
    CHECK(pp_project_add_item(project, PP_ANCHOR_TOP_LEFT, &image, NULL, &error));
    PPBlob font = read_bytes(PP_FONT_FIXTURES "/PointyFixture.ttf");
    /* Exercise actual nondefault permission metadata, using owned fixture bytes. */
    unsigned tables = (font.data[4] << 8) | font.data[5];
    bool patched = false;
    for (unsigned i = 0; i < tables; i++) {
        uint8_t *record = font.data + 12 + 16 * i;
        if (!memcmp(record, "OS/2", 4)) {
            size_t offset = be32(record + 8);
            CHECK(offset + 10 <= font.size);
            font.data[offset + 8] = 1; /* no-subsetting */
            font.data[offset + 9] = 8; /* editable embedding */
            patched = true;
            break;
        }
    }
    PPFontInfo info;
    CHECK(patched && pp_font_inspect((PPBytes){font.data, font.size}, 0, &info, &error));
    CHECK(info.embedding == PP_FONT_EDITABLE && info.no_subsetting && !info.bitmap_only);
    PPItem text = {0};
    text.kind = PP_ITEM_TEXT;
    text.text = "A";
    text.opacity = 1;
    text.text_style.requested_family = "Pointy Fixture";
    text.text_style.font_size_dip = 32;
    text.text_style.color[3] = 255;
    CHECK(pp_asset_import(assets, (PPBytes){font.data, font.size}, PP_ASSET_FONT,
                          &text.text_style.primary_asset, &error));
    pp_blob_destroy(&font);
    CHECK(pp_project_add_item(project, PP_ANCHOR_TOP_LEFT, &text, NULL, &error));
    for (int variant = 0; variant < 8; variant++) {
        CHECK(pp_project_save(project, package, &error));
        PPProject *complete = NULL;
        CHECK(pp_project_load(package, assets, &complete, &error));
        pp_project_destroy(complete);
        PPBlob saved = read_bytes(package);
        mz_zip_archive reader = {0}, writer = {0};
        CHECK(mz_zip_reader_init_mem(&reader, saved.data, saved.size, 0));
        CHECK(mz_zip_writer_init_heap(&writer, 0, 0));
        char hash[65], expected[80], replacement[80];
        hex_id(variant == 1 ? text.text_style.primary_asset : image.image_asset, hash);
        snprintf(expected, sizeof(expected), "%s/%s", variant == 1 ? "fonts" : "assets", hash);
        snprintf(replacement, sizeof(replacement), "%s/%s",
                 variant == 0   ? "fonts"
                 : variant == 1 ? "assets"
                                : "resources",
                 hash);
        unsigned renamed = 0;
        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&reader); i++) {
            mz_zip_archive_file_stat stat;
            CHECK(mz_zip_reader_file_stat(&reader, i, &stat));
            size_t size = 0;
            void *data = mz_zip_reader_extract_to_heap(&reader, i, &size, 0);
            CHECK(data);
            const char *name = stat.m_filename;
            char *altered = NULL;
            if (variant < 3 && !strcmp(name, expected)) {
                name = replacement;
                renamed++;
            }
            if (variant >= 3 && !strcmp(name, "manifest.json")) {
                altered = changed_metadata(data, size, variant, &size);
                renamed++;
            }
            CHECK(mz_zip_writer_add_mem(&writer, name, altered ? (void *)altered : data, size,
                                        MZ_BEST_SPEED));
            free(altered);
            mz_free(data);
        }
        CHECK(renamed == 1);
        void *repacked = NULL;
        size_t size = 0;
        CHECK(mz_zip_writer_finalize_heap_archive(&writer, &repacked, &size));
        CHECK(mz_zip_writer_end(&writer));
        CHECK(mz_zip_reader_end(&reader));
        pp_blob_destroy(&saved);
        write_bytes(package, repacked, size);
        mz_free(repacked);
        uint64_t revision = pp_project_revision(project);
        size_t retained = pp_assets_raster_bytes(assets);
        PPProject *candidate = NULL;
        CHECK(!pp_project_load(package, assets, &candidate, &error));
        CHECK(!candidate && error.code == PP_ERROR_FORMAT);
        CHECK(pp_project_revision(project) == revision &&
              pp_assets_raster_bytes(assets) == retained);
    }
    pp_project_destroy(project);
    pp_assets_destroy(assets);
}
