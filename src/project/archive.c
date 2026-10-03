#include "store_internal.h"
#include <stdlib.h>
#include <string.h>

static uint16_t u16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((unsigned)p[1] << 8));
}
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)u16(p) | ((uint32_t)u16(p + 2) << 16);
}
static bool read_at(FILE *file, uint64_t offset, void *data, size_t size) {
    return pp_platform_file_seek(file, offset) && fread(data, 1, size, file) == size;
}
static size_t zip_read(void *opaque, mz_uint64 offset, void *data, size_t size) {
    return read_at(opaque, offset, data, size) ? size : 0;
}
static bool valid_name(const char *name, size_t size) {
    if (size == 13 && !memcmp(name, "manifest.json", 13))
        return true;
    size_t prefix;
    if (size == 71 && !memcmp(name, "assets/", 7))
        prefix = 7;
    else if (size == 70 && !memcmp(name, "fonts/", 6))
        prefix = 6;
    else
        return false;
    PPAssetId id;
    return pp_store_id(name + prefix, &id);
}

/* Walk the central directory before miniz allocates its directory tables. ZIP64
 * and multi-disk containers are unnecessary inside these limits and rejected. */
bool pp_archive_open(FILE *file, mz_zip_archive *zip, PPError *error) {
    uint64_t length = 0;
    if (!pp_platform_file_size(file, &length))
        return pp_store_error(error, PP_ERROR_PLATFORM, "Cannot measure package");
    if (length < 22 || length > PP_STORE_COMPRESSED)
        return pp_store_error(error, PP_ERROR_RESOURCE_LIMIT,
                              "Package exceeds 256 MiB or is truncated");
    size_t tail_size = (size_t)(length < 65557 ? length : 65557);
    uint8_t *tail = malloc(tail_size);
    if (!tail)
        return pp_store_error(error, PP_ERROR_MEMORY, "Cannot read ZIP trailer");
    bool ok = false;
    uint64_t end = 0, cd = 0, cd_size = 0;
    unsigned count = 0;
    int failure_code = PP_ERROR_FORMAT;
    if (!read_at(file, length - tail_size, tail, tail_size))
        goto invalid;
    for (size_t p = tail_size - 22;; p--) {
        if (u32(tail + p) == 0x06054b50 && p + 22 + u16(tail + p + 20) == tail_size) {
            if (u16(tail + p + 4) || u16(tail + p + 6) || u16(tail + p + 8) != u16(tail + p + 10))
                goto invalid;
            count = u16(tail + p + 10);
            cd_size = u32(tail + p + 12);
            cd = u32(tail + p + 16);
            end = length - tail_size + p;
            break;
        }
        if (!p)
            goto invalid;
    }
    if (count > PP_STORE_ENTRIES) {
        failure_code = PP_ERROR_RESOURCE_LIMIT;
        goto invalid;
    }
    if (!count || cd > end || cd_size != end - cd)
        goto invalid;
    /* miniz recognizes a ZIP64 locator immediately before EOCD, even if those
     * bytes belong to a central-entry comment. Do not let it reinterpret the
     * bounded classic directory that this reader has validated. */
    if (end >= 20) {
        uint8_t signature[4];
        if (!read_at(file, end - 20, signature, sizeof(signature)) || u32(signature) == 0x07064b50)
            goto invalid;
    }
    char (*names)[PP_STORE_RESOURCE_NAME] = calloc(count, sizeof(*names));
    uint64_t *starts = calloc(count, sizeof(*starts)), *ends = calloc(count, sizeof(*ends));
    if (!names || !starts || !ends) {
        free(names);
        free(starts);
        free(ends);
        pp_store_error(error, PP_ERROR_MEMORY, "Cannot validate ZIP directory");
        goto done;
    }
    uint64_t pos = cd, total = 0;
    unsigned manifests = 0;
    for (unsigned i = 0; i < count; i++) {
        uint8_t h[46], local[30];
        if (pos > end || end - pos < 46 || !read_at(file, pos, h, sizeof(h)) ||
            u32(h) != 0x02014b50)
            goto bad_directory;
        size_t n = u16(h + 28), extra = u16(h + 30), comment = u16(h + 32);
        uint64_t record = 46 + (uint64_t)n + extra + comment;
        if (n > PP_STORE_NAME) {
            failure_code = PP_ERROR_RESOURCE_LIMIT;
            goto bad_directory;
        }
        if (!n || n >= PP_STORE_RESOURCE_NAME || record > end - pos || u16(h + 34))
            goto bad_directory;
        if (!read_at(file, pos + 46, names[i], n) || !valid_name(names[i], n))
            goto bad_directory;
        for (unsigned j = 0; j < i; j++)
            if (!strcmp(names[i], names[j]))
                goto bad_directory;
        uint32_t unpacked = u32(h + 24), packed = u32(h + 20), attributes = u32(h + 38);
        unsigned type = (attributes >> 16) & 0170000;
        if ((type && type != 0100000) || (attributes & 0x10) || (u16(h + 8) & (1 | 0x40)) ||
            (u16(h + 10) != 0 && u16(h + 10) != 8))
            goto bad_directory;
        bool manifest = !strcmp(names[i], "manifest.json");
        manifests += manifest;
        size_t limit = manifest ? PP_STORE_MANIFEST : PP_STORE_RESOURCE;
        if (unpacked > limit || packed > PP_STORE_COMPRESSED || total > PP_STORE_TOTAL - unpacked) {
            failure_code = PP_ERROR_RESOURCE_LIMIT;
            goto bad_directory;
        }
        if (!unpacked)
            goto bad_directory;
        total += unpacked;
        starts[i] = u32(h + 42);
        if (starts[i] > cd || cd - starts[i] < 30 ||
            !read_at(file, starts[i], local, sizeof(local)) || u32(local) != 0x04034b50)
            goto bad_directory;
        uint64_t header = 30 + (uint64_t)u16(local + 26) + u16(local + 28);
        if (u16(local + 26) != n || u16(local + 6) != u16(h + 8) || u16(local + 8) != u16(h + 10) ||
            header > cd - starts[i] || packed > cd - starts[i] - header)
            goto bad_directory;
        char local_name[PP_STORE_RESOURCE_NAME] = {0};
        if (!read_at(file, starts[i] + 30, local_name, n) || memcmp(local_name, names[i], n))
            goto bad_directory;
        ends[i] = starts[i] + header + packed;
        for (unsigned j = 0; j < i; j++)
            if (starts[i] < ends[j] && starts[j] < ends[i])
                goto bad_directory;
        pos += record;
    }
    if (pos != end || manifests != 1)
        goto bad_directory;
    zip->m_pRead = zip_read;
    zip->m_pIO_opaque = file;
    /* Exclude the already-validated global comment from miniz's search extent.
     * It searches for signatures without checking the final comment length;
     * otherwise a false EOCD inside a comment can select a different record. */
    if (!mz_zip_reader_init(zip, end + 22, 0))
        goto bad_directory;
    ok = true;
bad_directory:
    free(names);
    free(starts);
    free(ends);
    if (!ok)
        goto invalid;
    goto done;
invalid:
    pp_store_error(error, failure_code,
                   failure_code == PP_ERROR_RESOURCE_LIMIT
                       ? "ZIP directory exceeds package entry, name, or declared byte limits"
                       : "Invalid ZIP directory, entry, or path");
done:
    free(tail);
    return ok;
}

typedef struct PPExtraction {
    uint8_t *data;
    size_t size, written;
} PPExtraction;
static size_t extract_chunk(void *opaque, mz_uint64 offset, const void *data, size_t size) {
    PPExtraction *out = opaque;
    if (offset != out->written || offset > out->size || size > out->size - (size_t)offset)
        return 0;
    memcpy(out->data + out->written, data, size);
    out->written += size;
    return size;
}
bool pp_archive_extract(mz_zip_archive *zip, mz_uint index, size_t limit, PPBlob *out,
                        PPError *error) {
    memset(out, 0, sizeof(*out));
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(zip, index, &stat) || !stat.m_uncomp_size ||
        stat.m_uncomp_size > limit)
        return pp_store_error(error, PP_ERROR_RESOURCE_LIMIT, "ZIP extraction exceeds entry limit");
    PPExtraction extracted = {malloc((size_t)stat.m_uncomp_size + 1), (size_t)stat.m_uncomp_size,
                              0};
    if (!extracted.data)
        return pp_store_error(error, PP_ERROR_MEMORY, "Cannot allocate bounded extraction");
    if (!mz_zip_reader_extract_to_callback(zip, index, extract_chunk, &extracted, 0) ||
        extracted.written != extracted.size) {
        free(extracted.data);
        return pp_store_error(error, PP_ERROR_FORMAT, "ZIP data, size, or CRC mismatch");
    }
    extracted.data[extracted.size] = 0;
    out->data = extracted.data;
    out->size = extracted.size;
    return true;
}

typedef struct PPWriter {
    FILE *file;
    const PPProjectStoreHooks *hooks;
    PPError *error;
} PPWriter;
static size_t write_chunk(void *opaque, mz_uint64 offset, const void *data, size_t size) {
    PPWriter *writer = opaque;
    if (offset > PP_STORE_COMPRESSED || size > PP_STORE_COMPRESSED - offset) {
        pp_store_error(writer->error, PP_ERROR_RESOURCE_LIMIT,
                       "Compressed package exceeds 256 MiB");
        return 0;
    }
    if (writer->hooks && writer->hooks->before_write &&
        !writer->hooks->before_write(writer->hooks->context, writer->error))
        return 0;
    if (!pp_platform_file_seek(writer->file, offset))
        return 0;
    return fwrite(data, 1, size, writer->file);
}
bool pp_archive_write(FILE *file, const char *json, size_t size, const PPManifest *manifest,
                      const PPProjectStoreHooks *hooks, PPError *error) {
    PPWriter writer = {file, hooks, error};
    mz_zip_archive zip = {0};
    zip.m_pWrite = write_chunk;
    zip.m_pIO_opaque = &writer;
    if (!mz_zip_writer_init(&zip, 0))
        return pp_store_error(error, PP_ERROR_MEMORY, "Cannot initialize ZIP writer");
    bool ok = mz_zip_writer_add_mem(&zip, "manifest.json", json, size, MZ_BEST_SPEED) != 0;
    for (size_t i = 0; ok && i < manifest->count; i++) {
        char name[PP_STORE_RESOURCE_NAME];
        pp_store_resource_name(manifest->resources[i].id, manifest->resources[i].kind, name);
        PPBytes bytes = pp_asset_source(manifest->resources[i].asset);
        ok = mz_zip_writer_add_mem(&zip, name, bytes.data, bytes.size, MZ_BEST_SPEED) != 0;
    }
    if (ok)
        ok = mz_zip_writer_finalize_archive(&zip) != 0;
    if (!mz_zip_writer_end(&zip))
        ok = false;
    if (!ok && (!error || !error->code))
        pp_store_error(error, PP_ERROR_PLATFORM, "Cannot write complete ZIP package");
    return ok;
}
