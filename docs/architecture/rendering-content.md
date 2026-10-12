# Retained content ownership

The app owns one lazy `PPContentSources` service beside its asset manager.
Runtime, anchor, Settings and drawer consumers borrow it. A standalone Settings
or drawer can create this owner through `pp_app_content_sources` without a
runtime. Consumers close before the service; the service cancels and joins its
worker before the app releases assets. Runtime closure and project epochs
cancel their consumers without draining or cancelling an unrelated UI consumer.
Full shutdown destroys Settings, runtime/drawer and then the app.

Snapshots hold immutable canonical content roots and separate appearance
leaves. The artwork digest includes source/font bytes, face, text and relevant
style. Item IDs, project revision/name, list position and pose do not identify
artwork. A requested target also includes physical raster dimensions, effective
glyph size and normalized canonical layout. Ownership validation separately
retains each consumer's current snapshot revision and latest target. Image
default-size edits can change sprite requests while a fixed-fit drawer remains
at the same target.

| Source | Shared CPU policy | Consumer presentation |
| --- | --- | --- |
| PNG | Original decoded RGBA, cached visual-alpha status | Retained native-resolution texture |
| GIF | Composited native frames; frame revision/visibility cached | Caller elapsed clock; compatible native contexts may share immutable frame textures |
| Bitmap text | Useful fixed-strike detail and canonical layout/raster origins | GPU scaling; no intermediate fractional-size reraster |
| Scalable/mixed text | Canonical seed plus latest private requested-size endpoint | Retain available seed/current artwork until a candidate commits |
| SVG | Owned cached seed plus requested endpoint; fractional viewport is canonical | Rounded raster dimensions never redefine logical aspect or grabbed point |

`pp_content_sources_poll` is the sole completion dispatcher, bounded to 32
results per owner call. Worker keys identify stable content entries, never
project items or manufactured sprites. Consumer observations do not consume
results. Equal requests share one preparation and one CPU charge. Cancellation,
acceptance and rejection belong to each consumer. If every owner rejects an
owned candidate, its pixels are released; the rejecting owner suppresses repeated
attempts, while a fresh consumer can prepare the same endpoint again.
Borrowed snapshot-backed pixels, including useful bitmap text, remain available
to other consumers after rejection. The snapshot keeps their CPU charge until
its last reference is released; rejection cannot reclaim that storage.

Each consumer holds at most desired, accepted and seed entries. An entry with
no references is released, so cache growth is bounded by three references per
live consumer rather than an unbounded history of sizes. Shared-service
visual-alpha scans occur once per frame revision; native PNG bindings validate
their newly acquired source once and retain that result. The worker admits one active and one
waiting job. Pressure defers immutable copying and reservation; owner updates
retry the latest target while keeping available artwork. Workers use copied
immutable text/style/font/SVG bytes, private font/rasterizer state and no shared
snapshot, mutable asset cache, GL context or GPU object.

The 512 MiB CPU ledger includes canonical text/source alpha, decoded resources,
owned SVG seeds, shared five-byte RGBA/source-alpha endpoints, pending candidate
reservations, native compatibility copies and field-upload scratch. Shared
entries are charged once. Candidates are capped at 64 MiB, captures at 32 MiB
each/64 MiB total, and worker scratch follows the 384 MiB bounded-output policy.
Library bookkeeping has existing source/XML bounds rather than an exact
process allocator cap. The 256 MiB app GPU ledger counts content source textures,
shared GIF textures, shaped-field source images and UI output targets, including
old/candidate overlap. Control-label atlases, driver/context allocations and
compositor backing are separate. Reservations happen before allocation;
rejection/teardown refunds only the caller's allocations.

Windows visible anchors and sprites use transparent direct GL windows with a
color-only shader and rectangular input. Translation and unchanged static
content reuse the source and native window without uploads or transform draws.
Owned source alpha validates visibility and finds the nearest visible grab
point for a small activation target. A preview press retains its prepared
snapshot through the pull. Removal/disable/zero opacity cancels its pending
press without spawning removed content or releasing another input owner.
Existing sprites keep snapshots after source edits/removal. Replacement reuses
the prepared snapshot, keeps center/rotation/opacity/longest side, adopts its
canonical aspect and restarts that sprite's clock.

Settings and drawer have independent UI contexts. `PPUIContentPreview` borrows
the owning UI context and retains an RGBA source plus a color-only output FBO;
GLuint names never cross these contexts. Premultiplied color survives both
sampling steps and is converted only after the final Nuklear texture sample,
at the existing straight-alpha blend boundary. Preview updates require the
owning current frame. Success and failure restore the frame's GL state/context;
a failed final restoration keeps the accepted output and exact GPU charge.
Different-item selection releases old pixels before preparation. Same-item
rejection keeps usable accepted artwork. Zero opacity, closure and obsolete
project consumers release their work/resources. Drafts and control IDs remain
the UI owners' responsibility.

Settings content commits restart its own playback clock; metadata/pose changes
preserve it. Drawer GIFs intentionally sample frame zero. Visible drawer slots
move by stable item ID through reorder and unused slots release their consumers.
Cocoa uses the same retained CPU policy and canonical mapping with one RGBA
compatibility readback into its native image provider. It does not transform
coverage for normal rectangular presentation. Native Mac execution is still
unverified. Explicit image export/source-alpha diagnostics retain the separate
RGBA-plus-coverage readback APIs.

See [preview checks](../testing/preview-retention.md) and
[performance acceptance](../testing/rendering-performance.md). Source/CPU/GL
correctness is separate from complete frame timing and compositor cadence.
