#include "helpers.h"
#include "pointy/scene.h"
static bool fail_io(void *context, PPError *e) {
    (void)context;
    e->code = PP_ERROR_PLATFORM;
    strcpy(e->message, "injected failure");
    return false;
}
static bool fail_third_write(void *context, PPError *e) {
    int *calls = context;
    if (++*calls < 3)
        return true;
    return fail_io(NULL, e);
}
static void compare_projects(PPProject *a, PPProject *b) {
    CHECK(!strcmp(pp_project_name(a), pp_project_name(b)));
    CHECK(!memcmp(pp_project_settings(a), pp_project_settings(b), sizeof(PPProjectSettings)));
    for (int i = 0; i < PP_ANCHOR_COUNT; i++) {
        const PPAnchorList *x = pp_project_anchor(a, (PPAnchor)i),
                           *y = pp_project_anchor(b, (PPAnchor)i);
        CHECK(x->count == y->count && x->enabled == y->enabled && x->default_id == y->default_id);
        for (size_t j = 0; j < x->count; j++) {
            CHECK(x->item_ids[j] == y->item_ids[j]);
            const PPItem *p = pp_project_item(a, x->item_ids[j]),
                         *q = pp_project_item(b, y->item_ids[j]);
            CHECK(p->kind == q->kind && p->angle_deg == q->angle_deg && p->opacity == q->opacity);
            CHECK(!strcmp(p->label ? p->label : "", q->label ? q->label : ""));
            if (p->kind == PP_ITEM_TEXT) {
                CHECK(!strcmp(p->text, q->text));
                CHECK(!strcmp(p->text_style.requested_family, q->text_style.requested_family));
                CHECK(p->text_style.primary_face == q->text_style.primary_face &&
                      p->text_style.fallback_face == q->text_style.fallback_face);
                CHECK(p->text_style.font_size_dip == q->text_style.font_size_dip);
                CHECK(!memcmp(p->text_style.color, q->text_style.color, 4));
                CHECK(!memcmp(&p->text_style.primary_asset, &q->text_style.primary_asset,
                              sizeof(PPAssetId)));
                CHECK(!memcmp(&p->text_style.fallback_asset, &q->text_style.fallback_asset,
                              sizeof(PPAssetId)));
            } else {
                CHECK(p->image_longest_dip == q->image_longest_dip);
                CHECK(!memcmp(&p->image_asset, &q->image_asset, sizeof(PPAssetId)));
            }
            PPItemSnapshot *s = pp_project_snapshot(a, p->id), *t = pp_project_snapshot(b, q->id);
            const PPFrame *f, *g;
            double next;
            CHECK(pp_item_snapshot_frame(s, 0, 1, &f, &next, &error));
            CHECK(pp_item_snapshot_frame(t, 0, 1, &g, &next, &error));
            CHECK(f->width == g->width && f->height == g->height && f->stride == g->stride);
            CHECK(!memcmp(f->pixels, g->pixels, f->stride * f->height));
            pp_item_snapshot_release(s);
            pp_item_snapshot_release(t);
        }
    }
}
static void assert_reopens(const uint8_t hash[32]) {
    uint8_t current[32];
    digest_file(package, current);
    CHECK(!memcmp(hash, current, 32));
    PPAssets *assets = NULL;
    PPProject *loaded = NULL;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_load(package, assets, &loaded, &error));
    pp_project_destroy(loaded);
    CHECK(!pp_assets_external_raster_bytes(assets));
    pp_assets_destroy(assets);
}
static void test_portable_round_trip(void) {
    PPAssets *assets = NULL, *fresh = NULL;
    PPProject *project = NULL, *loaded = NULL;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_create(assets, &project, &error));
    CHECK(pp_project_set_name(project, "Pr\xc3\xa9sentation \xf0\x9f\x91\x89", &error));
    PPBlob font = read_bytes(PP_FONT_FIXTURES "/PointyFixture.ttc"),
           noto = read_bytes(PP_RESOURCES "/fonts/NotoColorEmoji.ttf");
    char owned[MAX_PATH], moved[MAX_PATH], source_path[MAX_PATH];
    snprintf(owned, sizeof(owned), "%s/sources", directory);
    snprintf(moved, sizeof(moved), "%s/moved", directory);
    CHECK(SDL_CreateDirectory(owned));
    snprintf(source_path, sizeof(source_path), "%s/font.ttc", owned);
    write_bytes(source_path, font.data, font.size);
    pp_blob_destroy(&font);
    font = read_bytes(source_path);
    snprintf(source_path, sizeof(source_path), "%s/noto.ttf", owned);
    write_bytes(source_path, noto.data, noto.size);
    pp_blob_destroy(&noto);
    noto = read_bytes(source_path);
    PPItem image = {0}, text = {0};
    image.kind = PP_ITEM_IMAGE;
    image.label = "Diagram";
    image.image_longest_dip = 75;
    image.opacity = .7;
    image.angle_deg = 20;
    const char *svg = "<svg width='40' height='20'><rect width='40' height='20' fill='red'/></svg>";
    snprintf(source_path, sizeof(source_path), "%s/image.svg", owned);
    write_bytes(source_path, svg, strlen(svg));
    PPBlob image_bytes = read_bytes(source_path);
    CHECK(pp_asset_import(assets, (PPBytes){image_bytes.data, image_bytes.size}, PP_ASSET_SVG,
                          &image.image_asset, &error));
    pp_blob_destroy(&image_bytes);
    text.kind = PP_ITEM_TEXT;
    text.label = "Presenter hand";
    text.text = "A\xf0\x9f\x91\x89";
    text.opacity = 1;
    text.text_style.font_size_dip = 48;
    text.text_style.primary_face = 1;
    text.text_style.requested_family = "Pointy Fixture Wide";
    text.text_style.color[3] = 255;
    CHECK(pp_asset_import(assets, (PPBytes){font.data, font.size}, PP_ASSET_FONT,
                          &text.text_style.primary_asset, &error));
    CHECK(pp_asset_import(assets, (PPBytes){noto.data, noto.size}, PP_ASSET_FONT,
                          &text.text_style.fallback_asset, &error));
    CHECK(pp_project_set_notice(project, text.text_style.primary_asset, "Pointy fixture OFL notice",
                                &error));
    CHECK(
        pp_project_set_notice(project, text.text_style.fallback_asset, "Noto OFL notice", &error));
    pp_blob_destroy(&font);
    pp_blob_destroy(&noto);
    for (int a = 0; a < 8; a++) {
        uint64_t id;
        CHECK(pp_project_add_item(project, (PPAnchor)a, &image, NULL, &error));
        CHECK(pp_project_add_item(project, (PPAnchor)a, &text, &id, &error));
        CHECK(pp_project_set_default(project, (PPAnchor)a, id, &error));
        CHECK(pp_project_reorder_item(project, (PPAnchor)a, id, 0, &error));
    }
    CHECK(pp_project_set_enabled(project, PP_ANCHOR_TOP_LEFT, false, &error));
    CHECK(pp_project_save(project, package, &error));
    CHECK(SDL_RenamePath(owned, moved));
    CHECK(pp_assets_create(&fresh, &error));
    CHECK(pp_project_load(package, fresh, &loaded, &error));
    compare_projects(project, loaded);
    CHECK(!strcmp(pp_project_notice(loaded, text.text_style.primary_asset),
                  "Pointy fixture OFL notice"));
    PPBlob zipbytes = read_bytes(package);
    mz_zip_archive zip = {0};
    CHECK(mz_zip_reader_init_mem(&zip, zipbytes.data, zipbytes.size, 0));
    CHECK(mz_zip_reader_get_num_files(&zip) == 4);
    size_t n;
    char *extracted = mz_zip_reader_extract_file_to_heap(&zip, "manifest.json", &n, 0);
    CHECK(extracted);
    char *json = malloc(n + 1);
    CHECK(json);
    memcpy(json, extracted, n);
    json[n] = 0;
    mz_free(extracted);
    CHECK(!strstr(json, "scene") && !strstr(json, "window") && !strstr(json, "display") &&
          !strstr(json, directory));
    yyjson_doc *manifest = yyjson_read(json, n, 0);
    CHECK(manifest);
    yyjson_val *resources = yyjson_obj_get(yyjson_doc_get_root(manifest), "resources");
    size_t resource_index, resource_count, fonts = 0;
    yyjson_val *resource;
    char primary_hash[65];
    hex_id(text.text_style.primary_asset, primary_hash);
    yyjson_arr_foreach(resources, resource_index, resource_count, resource) {
        yyjson_val *faces = yyjson_obj_get(resource, "faces");
        if (!strcmp(yyjson_get_str(yyjson_obj_get(resource, "kind")), "font")) {
            fonts++;
            bool collection =
                !strcmp(yyjson_get_str(yyjson_obj_get(resource, "sha256")), primary_hash);
            CHECK(yyjson_is_arr(faces) && yyjson_arr_size(faces) == (collection ? 2 : 1));
            size_t face_index, face_count;
            yyjson_val *face;
            yyjson_arr_foreach(faces, face_index, face_count, face) {
                const char *embedding = yyjson_get_str(yyjson_obj_get(face, "embedding"));
                CHECK(embedding && !strcmp(embedding, "installable"));
                CHECK(yyjson_is_false(yyjson_obj_get(face, "no_subsetting")));
                CHECK(yyjson_is_false(yyjson_obj_get(face, "bitmap_only")));
            }
        } else
            CHECK(!faces);
    }
    CHECK(fonts == 2);
    yyjson_doc_free(manifest);
    char resource_hash[65], expected_name[80];
    hex_id(image.image_asset, resource_hash);
    snprintf(expected_name, sizeof(expected_name), "assets/%s", resource_hash);
    CHECK(mz_zip_reader_locate_file(&zip, expected_name, NULL, 0) >= 0);
    hex_id(text.text_style.primary_asset, resource_hash);
    snprintf(expected_name, sizeof(expected_name), "fonts/%s", resource_hash);
    CHECK(mz_zip_reader_locate_file(&zip, expected_name, NULL, 0) >= 0);
    hex_id(text.text_style.fallback_asset, resource_hash);
    snprintf(expected_name, sizeof(expected_name), "fonts/%s", resource_hash);
    CHECK(mz_zip_reader_locate_file(&zip, expected_name, NULL, 0) >= 0);
    for (mz_uint i = 0; i < 4; i++) {
        mz_zip_archive_file_stat st;
        CHECK(mz_zip_reader_file_stat(&zip, i, &st));
        CHECK(!strchr(st.m_filename, ':') && st.m_filename[0] != '/');
        CHECK(strcmp(st.m_filename, "manifest.json") == 0 ||
              strncmp(st.m_filename, "assets/", 7) == 0 ||
              strncmp(st.m_filename, "fonts/", 6) == 0);
    }

    free(json);
    mz_zip_reader_end(&zip);
    pp_blob_destroy(&zipbytes);
    uint8_t hash[32];
    digest_file(package, hash);
    uint64_t revision = pp_project_revision(project);
    for (int phase = 0; phase < 3; phase++) {
        PPProjectStoreHooks hooks = {0};
        int calls = 0;
        hooks.context = &calls;
        if (phase == 0)
            hooks.before_write = fail_third_write;
        if (phase == 1)
            hooks.before_flush = fail_io;
        if (phase == 2)
            hooks.before_replace = fail_io;
        CHECK(!pp_project_save_with_hooks(project, package, &hooks, &error));
        if (phase == 0)
            CHECK(calls == 3);
        assert_reopens(hash);
        CHECK(pp_project_revision(project) == revision);
    }
#ifdef _WIN32
    /* Windows denies replacement of a read-only destination. POSIX rename
     * permission is controlled by its directory, not the destination mode. */
    wchar_t wide[MAX_PATH];
    CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, package, -1, wide, MAX_PATH));
    CHECK(SetFileAttributesW(wide, FILE_ATTRIBUTE_READONLY));
    CHECK(!pp_project_save(project, package, &error));
    assert_reopens(hash);
    CHECK(SetFileAttributesW(wide, FILE_ATTRIBUTE_NORMAL));
#endif
    char sentinel[MAX_PATH];
    snprintf(sentinel, sizeof(sentinel), "%s/sentinel.tmp", directory);
    write_bytes(sentinel, "keep", 4);
    CHECK(!pp_project_save(project, directory, &error));
    assert_reopens(hash);
    PPBlob kept = read_bytes(sentinel);
    CHECK(kept.size == 4);
    pp_blob_destroy(&kept);
    CHECK(pp_platform_remove_file(sentinel, &error));
    pp_project_destroy(loaded);
    pp_assets_destroy(fresh);
    pp_project_destroy(project);
    CHECK(!pp_assets_external_raster_bytes(assets));
    pp_assets_destroy(assets);
    const char *filenames[] = {"font.ttc", "noto.ttf", "image.svg"};
    for (size_t i = 0; i < 3; i++) {
        snprintf(source_path, sizeof(source_path), "%s/%s", moved, filenames[i]);
        CHECK(pp_platform_remove_file(source_path, &error));
    }
    CHECK(SDL_RemovePath(moved));
}
static void test_missing_aptos_default_and_unresolved_text(void);
static void test_candidate_pressure_and_notice_isolation(void);
static void test_named_instance_save_preserves_original(void);
static void test_font_metadata_budget(void);
int main(void) {
    begin_test();
    test_portable_round_trip();
    test_missing_aptos_default_and_unresolved_text();
    test_candidate_pressure_and_notice_isolation();
    test_named_instance_save_preserves_original();
    test_font_metadata_budget();
    end_test();
    puts("portable round trip and atomic failures passed");
    return 0;
}

static void test_missing_aptos_default_and_unresolved_text(void) {
    PPAssets *assets = NULL, *fresh = NULL;
    PPProject *project = NULL, *loaded = NULL;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_create(assets, &project, &error));
    PPBlob noto = read_bytes(PP_RESOURCES "/fonts/NotoColorEmoji.ttf"),
           notice = read_bytes(PP_RESOURCES "/licenses/NotoEmoji-OFL.txt");
    PPItem item = {0};
    item.kind = PP_ITEM_TEXT;
    item.text = "\xf0\x9f\x91\x89";
    item.opacity = 1;
    item.text_style.requested_family = "Aptos";
    item.text_style.font_size_dip = 96;
    item.text_style.color[3] = 255;
    CHECK(pp_asset_import(assets, (PPBytes){noto.data, noto.size}, PP_ASSET_FONT,
                          &item.text_style.fallback_asset, &error));
    CHECK(pp_project_set_notice(project, item.text_style.fallback_asset, (const char *)notice.data,
                                &error));
    pp_blob_destroy(&noto);
    pp_blob_destroy(&notice);
    uint64_t id;
    CHECK(pp_project_add_item(project, PP_ANCHOR_TOP_LEFT, &item, &id, &error));
    CHECK(pp_project_save(project, package, &error));
    CHECK(pp_assets_create(&fresh, &error));
    CHECK(pp_project_load(package, fresh, &loaded, &error));
    compare_projects(project, loaded);
    CHECK(
        strstr(pp_project_notice(loaded, item.text_style.fallback_asset), "SIL OPEN FONT LICENSE"));
    uint8_t hash[32];
    const char *covered[] = {"\xf0\x9f\x91\x89\xf0\x9f\x91\x89",
                            "\xf0\x9f\x98\x80", "\xf0\x9f\x91\x89\n\xf0\x9f\x98\x80",
                            "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb",
                            "\xf0\x9f\x91\x89\xef\xb8\x8f"};
    for (size_t i = 0; i < sizeof(covered) / sizeof(covered[0]); i++) {
        item.text = covered[i];
        CHECK(pp_project_edit_item(project, id, &item, &error));
        PPFrame frame = {0}; PPTextDiagnostics diagnostics;
        CHECK(pp_text_render(assets, item.text, &item.text_style, 1, &frame, &diagnostics, &error));
        CHECK(diagnostics.missing_requested_families && !diagnostics.missing_glyphs);
        pp_frame_destroy(&frame);
        CHECK(pp_project_save(project, package, &error));
        pp_project_destroy(loaded); pp_assets_destroy(fresh);
        CHECK(pp_assets_create(&fresh, &error));
        CHECK(pp_project_load(package, fresh, &loaded, &error));
        compare_projects(project, loaded);
        CHECK(strstr(pp_project_notice(loaded, item.text_style.fallback_asset), "SIL OPEN FONT LICENSE"));
    }
    digest_file(package, hash);
    /* Noto cannot cover A; rendering .notdef is allowed by the editor but must
       never turn an unresolved family/glyph into a portable saved project. */
    item.text = "A\xf0\x9f\x91\x89";
    CHECK(pp_project_edit_item(project, id, &item, &error));
    CHECK(!pp_project_save(project, package, &error));
    assert_reopens(hash);
    item.text = "\xf0\x9f\x91\x89\xf4\x8f\xbf\xbf";
    CHECK(pp_project_edit_item(project, id, &item, &error));
    CHECK(!pp_project_save(project, package, &error));
    assert_reopens(hash);
    item.text = "\xf0\x9f\x91\x89";
    item.text_style.requested_family = "Absent Other Family";
    CHECK(pp_project_edit_item(project, id, &item, &error));
    CHECK(!pp_project_save(project, package, &error));
    assert_reopens(hash);
    pp_project_destroy(loaded);
    pp_assets_destroy(fresh);
    pp_project_destroy(project);
    pp_assets_destroy(assets);
}
static void test_candidate_pressure_and_notice_isolation(void) {
    PPAssets *assets = NULL;
    PPProject *active = NULL, *candidate = NULL;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_create(assets, &active, &error));
    PPBlob font = read_bytes(PP_FONT_FIXTURES "/PointyFixture.ttf");
    PPItem item = {0};
    item.kind = PP_ITEM_TEXT;
    item.text = "A";
    item.opacity = 1;
    item.text_style.requested_family = "Pointy Fixture";
    item.text_style.font_size_dip = 32;
    item.text_style.color[3] = 255;
    CHECK(pp_asset_import(assets, (PPBytes){font.data, font.size}, PP_ASSET_FONT,
                          &item.text_style.primary_asset, &error));
    pp_blob_destroy(&font);
    CHECK(pp_project_set_notice(active, item.text_style.primary_asset, "saved notice", &error));
    CHECK(pp_project_add_item(active, PP_ANCHOR_TOP_LEFT, &item, NULL, &error));
    CHECK(pp_project_save(active, package, &error));
    CHECK(pp_project_set_notice(active, item.text_style.primary_asset, "active notice", &error));
    PPScene *scene = NULL;
    uint64_t instance;
    CHECK(pp_scene_create(&scene, &error));
    CHECK(pp_scene_spawn(scene, active, PP_ANCHOR_TOP_LEFT, (PPVec2){123, 456}, 10, &instance,
                         &error));
    PPItemSnapshot *snapshot = pp_scene_get(scene, instance)->snapshot;
    uint64_t revision = pp_project_revision(active);
    uint8_t hash[32];
    digest_file(package, hash);
    PPProjectStoreHooks hooks = {0};
    hooks.before_replace = fail_io;
    CHECK(!pp_project_save_with_hooks(active, package, &hooks, &error));
    assert_reopens(hash);
    CHECK(pp_project_revision(active) == revision);
    CHECK(pp_scene_count(scene) == 1 && pp_scene_get(scene, instance)->snapshot == snapshot);
    size_t base = pp_assets_raster_bytes(assets), capacity = (size_t)512 * 1024 * 1024;
    size_t first = 256 * 1024 * 1024, second = capacity - base - first;
    CHECK(pp_assets_reserve_raster(assets, first, &error));
    CHECK(pp_assets_reserve_raster(assets, second, &error));
    CHECK(!pp_project_load(package, assets, &candidate, &error));
    CHECK(!candidate);
    CHECK(pp_assets_raster_bytes(assets) == capacity);
    CHECK(pp_project_revision(active) == revision);
    CHECK(pp_scene_count(scene) == 1 && pp_scene_get(scene, instance)->snapshot == snapshot);
    CHECK(pp_scene_get(scene, instance)->pose.center.x == 123 &&
          pp_scene_get(scene, instance)->pose.center.y == 456);
    CHECK(!strcmp(pp_project_notice(active, item.text_style.primary_asset), "active notice"));
    pp_assets_release_raster(assets, second);
    pp_assets_release_raster(assets, first);
    CHECK(pp_project_load(package, assets, &candidate, &error));
    CHECK(!strcmp(pp_project_notice(candidate, item.text_style.primary_asset), "saved notice"));
    CHECK(!strcmp(pp_project_notice(active, item.text_style.primary_asset), "active notice"));
    pp_project_destroy(candidate);
    CHECK(pp_assets_raster_bytes(assets) == base);
    pp_scene_destroy(scene);
    pp_project_destroy(active);
    CHECK(!pp_assets_external_raster_bytes(assets));
    pp_assets_destroy(assets);
}

static void test_named_instance_save_preserves_original(void) {
    PPBlob font = read_bytes(PP_FONT_FIXTURES "/PointyVariable.ttf");
    PPFontInfo info;
    CHECK(pp_font_inspect((PPBytes){font.data, font.size}, 65536, &info, &error));
    CHECK(info.face_count == 1 && info.embeddable);
    PPAssets *assets = NULL;
    PPProject *project = NULL;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_create(assets, &project, &error));
    PPItem item = {0};
    item.kind = PP_ITEM_TEXT;
    item.text = "A";
    item.opacity = 1;
    item.text_style.requested_family = "Pointy Fixture";
    item.text_style.font_size_dip = 32;
    item.text_style.color[3] = 255;
    CHECK(pp_asset_import(assets, (PPBytes){font.data, font.size}, PP_ASSET_FONT,
                          &item.text_style.primary_asset, &error));
    pp_blob_destroy(&font);
    uint64_t id;
    CHECK(pp_project_add_item(project, PP_ANCHOR_TOP_LEFT, &item, &id, &error));
    CHECK(pp_project_save(project, package, &error));
    uint8_t hash[32];
    digest_file(package, hash);
    PPScene *scene = NULL;
    uint64_t instance;
    CHECK(pp_scene_create(&scene, &error));
    CHECK(
        pp_scene_spawn(scene, project, PP_ANCHOR_TOP_LEFT, (PPVec2){50, 60}, 0, &instance, &error));
    PPItemSnapshot *snapshot = pp_scene_get(scene, instance)->snapshot;
    item.text_style.primary_face = 65536;
    /* The model and SDL_ttf can render this genuine named instance. The storage
       schema allows collection indices only, so portability must reject it. */
    CHECK(pp_project_edit_item(project, id, &item, &error));
    uint64_t revision = pp_project_revision(project);
    size_t retained = pp_assets_raster_bytes(assets);
    CHECK(!pp_project_save(project, package, &error));
    CHECK(error.code == PP_ERROR_FORMAT);
    assert_reopens(hash);
    CHECK(pp_project_revision(project) == revision &&
          pp_project_item(project, id)->text_style.primary_face == 65536);
    CHECK(pp_assets_raster_bytes(assets) == retained);
    CHECK(pp_scene_count(scene) == 1 && pp_scene_get(scene, instance)->snapshot == snapshot);
    CHECK(pp_scene_get(scene, instance)->pose.center.x == 50 &&
          pp_scene_get(scene, instance)->pose.center.y == 60);
    pp_scene_destroy(scene);
    pp_project_destroy(project);
    CHECK(!pp_assets_external_raster_bytes(assets));
    pp_assets_destroy(assets);
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void write_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}
static void test_font_metadata_budget(void) {
    PPBlob font = read_bytes(PP_FONT_FIXTURES "/PointyFixture.ttf");
    const uint32_t faces = 4096, header = 12 + 4 * faces;
    /* FreeType requires room for at least 28 bytes per declared face header,
       even when valid collection offsets share the same directory. */
    size_t collection_size = header + font.size + 28 * faces + 1;
    PPBlob collection = {calloc(1, collection_size), collection_size};
    CHECK(collection.data);
    memcpy(collection.data, "ttcf", 4);
    write_be32(collection.data + 4, 0x10000);
    write_be32(collection.data + 8, faces);
    for (uint32_t i = 0; i < faces; i++)
        write_be32(collection.data + 12 + 4 * i, header);
    memcpy(collection.data + header, font.data, font.size);
    unsigned tables = (font.data[4] << 8) | font.data[5];
    for (unsigned i = 0; i < tables; i++) {
        uint8_t *record = collection.data + header + 12 + 16 * i;
        write_be32(record + 8, read_be32(record + 8) + header);
    }
    pp_blob_destroy(&font);
    PPFontInfo info;
    CHECK(pp_font_inspect((PPBytes){collection.data, collection.size}, 4095, &info, &error));
    CHECK(info.face_count == faces && info.embeddable);
    PPAssets *assets = NULL;
    PPProject *project = NULL;
    CHECK(pp_assets_create(&assets, &error));
    CHECK(pp_project_create(assets, &project, &error));
    PPItem item = {0};
    item.kind = PP_ITEM_TEXT;
    item.text = "A";
    item.opacity = 1;
    item.text_style.requested_family = "Pointy Fixture";
    item.text_style.font_size_dip = 16;
    item.text_style.color[3] = 255;
    uint8_t original[32];
    for (unsigned resource = 0; resource < 5; resource++) {
        /* Distinct owned sources sharing legal face offsets exercise the
           resource_count * face_count bound without large font payloads. */
        collection.data[collection.size - 1] = (uint8_t)resource;
        CHECK(pp_asset_import(assets, (PPBytes){collection.data, collection.size}, PP_ASSET_FONT,
                              &item.text_style.primary_asset, &error));
        CHECK(pp_project_add_item(project, PP_ANCHOR_TOP_LEFT, &item, NULL, &error));
        if (resource == 3) {
            CHECK(pp_project_save(project, package, &error));
            digest_file(package, original);
        }
    }
    uint64_t revision = pp_project_revision(project);
    CHECK(!pp_project_save(project, package, &error));
    CHECK(error.code == PP_ERROR_RESOURCE_LIMIT);
    CHECK(strstr(error.message, "metadata"));
    CHECK(pp_project_revision(project) == revision);
    assert_reopens(original);
    pp_project_destroy(project);
    pp_assets_destroy(assets);
    pp_blob_destroy(&collection);
}
