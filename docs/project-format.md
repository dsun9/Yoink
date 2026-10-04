# PointyPointer project format, schema 1

A `.pointy` file is a self-contained ZIP package. It stores the shared eight lists, item appearance, defaults, interaction settings, owned image/font bytes, and project-specific resource notices. Placed sprites, scenes, window positions, display IDs, source paths, and network references are absent.

## Container

The only entry names are `manifest.json`, `assets/<sha256>` for PNG/GIF/SVG sources, and `fonts/<sha256>` for complete font sources. `<sha256>` is the lowercase 64-character SHA-256 digest of the complete uncompressed source bytes. Repeated resources have one entry, even when every list uses them. The resource kind determines its namespace; an image under `fonts/`, a font under `assets/`, or the obsolete `resources/` namespace is rejected. No resource is extracted to a filesystem path.

The reader accepts stored and deflated entries in single-disk, non-ZIP64 containers. It rejects duplicate names, unsupported/encrypted entries, directory and symlink entries, unsafe/noncanonical names, overlapping local data, mismatched local names, bad CRCs, missing/orphan resources, and digest mismatches. Local and central directory bounds are validated before miniz extraction.

Preflight chooses the end-of-central-directory record whose declared comment reaches the physical file end. Miniz receives only the extent through that record's 22 fixed bytes, excluding the validated global comment from its signature search. A ZIP64 locator immediately before that record is rejected, even when hidden in a central-entry comment. This keeps both parsers on the same bounded classic directory.

| Limit | Maximum |
| --- | ---: |
| Compressed package file | 256 MiB |
| Total declared/extracted bytes, including manifest | 512 MiB |
| One source resource | 64 MiB |
| Manifest UTF-8 JSON | 2 MiB |
| ZIP entries, including manifest | 4096 |
| ZIP entry-name declaration | 1024 bytes |
| Project items across all lists | 4096 |
| Faces in one embedded font collection | 4096 |

Canonical names are shorter than the general name limit. JSON nesting is bounded to 16 levels; duplicate object keys, embedded NULs, unknown schema versions, wrong types, and noncanonical item fields are rejected. Decoded resources must also pass the media layer's dimension, frame-count, visibility, and raster-memory limits. Limits are checked using bounded arithmetic and extraction callbacks.

## Manifest

The root contains exactly `schema`, `name`, `settings`, `resources`, and `lists`. `schema` is the integer `1`.

`name` is the owned project name, independent of its package filename. It is a
required UTF-8 string of 1–255 bytes, with no embedded NUL or control characters
and at least one non-whitespace character. New projects use `Untitled project`.
Save As changes the filename without changing this name. This unreleased schema
rejects missing, malformed, blank, or oversized names rather than inventing one.

`settings` contains the four canonical values: `activation_dip`, `rotation_step_deg`, `resize_factor`, and `easing_seconds`. Their defaults are 16, 10, 1.1, and 0.12. Existing project-model ranges apply.

`resources` is an array of objects with `sha256`, `kind` (`png`, `gif`, `svg`, or `font`), and `notice` (UTF-8 attribution/license text, possibly empty). The SHA-256 identifies bytes, and is independent of the notice. Notices belong to each project: two projects may use the same immutable source bytes with different notices.

Font resource objects also require a `faces` array. Its length is exactly the actual font collection face count, and each ordered record describes the face at that array index. Image resource objects cannot contain `faces`. A face record contains exactly these actual-byte-derived fields:

```json
{"embedding":"installable","no_subsetting":false,"bitmap_only":false}
```

`embedding` records `installable`, `editable`, `preview_only`, or `restricted`; the two flags are booleans. Save derives every record from font inspection. Load re-inspects every face and requires all three values and the array length to match, while independently rejecting disallowed embedding permissions. Metadata cannot authorize restricted font bytes. Before allocating each font's metadata array, save charges 128 bytes per face against an aggregate 2 MiB construction budget, limiting the total to 16,384 face records across resources. This conservative bound prevents resource-count times face-count expansion before the final serialized manifest-size check. The per-font 4096-face limit also applies.

`lists` has exactly eight entries, in this order: top left, top center, top right, center left, center right, bottom left, bottom center, bottom right. Each has an `enabled` boolean, a `default` item ID, and an ordered `items` array. An empty list has default zero. A nonempty list's default must identify one of its items. All item IDs are unique nonzero unsigned 64-bit integers and survive loading. JSON consumers must preserve integer precision.

Each image item contains exactly:

```json
{
  "id": 1,
  "kind": "image",
  "angle": 0,
  "opacity": 1,
  "image_longest_dip": 96,
  "resource": "<64-character source digest>"
}
```

Each text item contains exactly:

```json
{
  "id": 2,
  "kind": "text",
  "angle": 0,
  "opacity": 1,
  "text": "A",
  "requested_family": "Pointy Fixture",
  "font_size_dip": 96,
  "color": [0, 0, 0, 255],
  "primary": {"resource": "<64-character font digest>", "face": 0},
  "fallback": null
}
```

`primary` and `fallback` are either null or font-resource/collection-face pairs. The selected face is a nonnegative integer smaller than the actual collection face count. FreeType's encoded named-instance selectors are not collection indices and are rejected. Color channels are bytes. Text has one canonical font size; images have one canonical longest-side size. There are no competing size fields. Rotation, opacity, and size retain the validated project's values.

## Fonts and portability

Save and load inspect actual font bytes, including every face in a collection, through FreeType. Every embedded face must permit installable or editable full-font embedding. Restricted, preview-only, and bitmap-only embedding permissions are rejected. A no-subsetting flag is respected by preserving the complete original source file. Font name/license tables, complete collection bytes, and selected face indices are preserved; manifest metadata cannot override embedding permissions.

The actual rendered text must have no missing glyphs, and requested families must resolve. The default hand `👉` has one explicit exception: requested-family metadata may remain `Aptos` when the primary font is absent, the embedded fallback is Noto Color Emoji, and every rendered glyph resolves. Other missing families or uncovered glyphs prevent portable save/load. The application bundle contains Noto Color Emoji and its OFL notice; it does not bundle the development machine's Aptos. A user-selected, permitted installed font can be embedded in that user's project. Default-project construction attaches the bundled Noto OFL, and explicit imports can attach their notices using `pp_project_set_notice`.

## Transactions, native I/O, and ownership

`pp_project_load(path, assets, &candidate, error)` is synchronous and returns a separate complete candidate. The caller must serialize storage with all mutations of the app's asset manager, and only swap the active project after success. Failure leaves `candidate` null. The app-owned `PPAssets` manager must outlive all projects and scene snapshots.

The loader validates the entire archive and model in a temporary asset manager. It reserves the app manager's existing raster usage in the temporary manager before decoding, limiting combined retained CPU rasters to 512 MiB. This is conservative under pressure, including app caches that could otherwise be evicted. Staged snapshots are destroyed and unretained caches are trimmed before source bytes transfer to the app manager; temporary reservations and sources are always released on exit. Imported resources are decoded again under the app's normal budget. A late allocation failure during transfer can leave zero-retain imported sources in the app manager until destruction, and zero-retain raster caches until eviction. Active project/scene objects and their retained snapshots remain unchanged.

The raster limit counts retained RGBA/coverage and explicitly charged presenter memory. It does **not** count source copies, the bounded ZIP directory, extraction buffers, JSON representation, temporary font-inspection/text frames, decoder scratch, or GPU memory. Source totals and individual extraction buffers have the separate archive bounds above; existing app-owned source bytes persist according to the asset contract.

`pp_project_save(project, path, error)` creates an exclusively owned temporary file next to the target. It writes and finalizes the ZIP, flushes the C stream and native storage handle, closes the writer, and then atomically replaces the target. Only that operation's temporary file is removed on failure. Project revision/dirty tracking and scenes are not mutated; callers update their saved-revision marker only on success. A failed cleanup does not justify deleting other files. An abrupt process termination can leave an unreferenced temporary file, while replacement happens only after a complete flush/close.

Windows paths are decoded strictly from UTF-8 and passed to wide-path APIs. Replacement uses same-directory `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`; no delete-then-rename sequence is used. Atomicity and durability depend on the destination filesystem's native rename/flush guarantees. The macOS file adapter remains Task 10 work and has not been hardware-verified here.

`PPProjectStoreHooks` and `pp_project_save_with_hooks` are a test-only seam for write, flush, and replacement failures. Ordinary API calls use native defaults. The storage tests verify old-file SHA-256 and reopen after each injected failure, including a partial write and a read-only target.
