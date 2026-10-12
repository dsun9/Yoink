# SVG endpoint retention

[Ticket #9](https://github.com/dsun9/Yoink/issues/9) uses the
[bounded scalable worker](scalable-retention.md) for Windows SVG sprites.
Each sprite keeps its own CPU endpoint and retained GL texture. Fractional
wheel easing, rotation, opacity and position reuse available artwork. Resize
input requests the current endpoint immediately, and owner updates publish a
matching completion during easing without a debounce delay.

Initial presentation borrows the last validated shared cache without asking
the owner to render another size. If that cache was evicted before the item
retained its asset, the sprite stays hidden and cannot intercept input until
its first worker result arrives.

The owner copies immutable source bytes and canonical viewport dimensions.
`pp_svg_render_bounded` validates the existing supported SVG subset, creates
one private PlutoSVG document, and renders directly at the requested dimensions.
It does not import into a private asset manager or generate an intrinsic raster.
The worker has no shared asset, snapshot, reservation or GL reference. Private
document and PlutoVG surface destruction finish before its result is returned.

Rounded requested pixel dimensions map to the complete canonical viewport.
SVG preflight keeps a fractional document viewport independently of the rounded
intrinsic cache dimensions, so a 13.5 by 7.5 viewport keeps that aspect.
The raster's aspect does not redefine the logical aspect, center, longest-side
size, source padding or grabbed point. The shared source service owns cached
seed copies and endpoint entries independently of the asset's compatibility
raster. Equal artwork/target requests share one CPU charge; different endpoints
retain their own mapping. See [shared preview checks](preview-retention.md).

The color-only Cocoa compatibility adapter and direct Windows anchor previews
also use the canonical SVG viewport. They compute rotated extents from that
viewport rather than rounded source pixels. A 13.5 by 7.5 SVG at 337 DIP and
17.25 degrees has a 378 by 279 pixel extent at density one; interpreting its
337 by 188 source pixels as the layout would incorrectly add a row. Native
Mac geometry remains unverified; the adapter builds and its public presenter
geometry check runs with a rendering boundary.

Worker SVG requests use app-service generation and stable content-entry IDs.
Consumers separately retain snapshot revision, latest endpoint and density;
the sole dispatcher routes entries and native/UI owners revalidate publication.
Cancellation, coalescing, bounded admission, project retirement and final join
follow the same owner contracts as scalable text. Successful private work counts
once, including completed work discarded after cancellation. A failed generator
or rejected candidate preserves usable artwork. A broken native/GL publication
removes the failed sprite and releases input interception.

When another sprite occupies the waiting slot, the presenter keeps cached
artwork and stores only the latest target key. It defers copying source bytes
and reservation until admission is available, then retries automatically on
an owner update. This preserves a newly pulled third sprite while one job is
running and a second is waiting. The worker still admits at most two captures.

The owner reserves exact candidate RGBA plus source alpha, five bytes per pixel,
before dispatch. Retained CPU bytes and candidates share the 512 MiB asset budget;
each candidate has a 64 MiB cap. The private SVG generator checks temporary
ARGB surface plus output overlap, nine bytes per pixel, against the independent
384 MiB worker scratch policy before allocating either. Captured bytes have the
service's 32 MiB per capture and 64 MiB aggregate limits. Library bookkeeping
remains constrained by source size and the existing XML node/reference/depth
limits, rather than reported as an exact process allocator cap. Old and candidate
GPU sources remain subject to the 256 MiB presenter budget. Empty SVG sources
hide their native window and release input.

The presenter seam holds production SVG generation at its public immutable
content boundary. It checks immediate publication, twenty-target coalescing,
drag/density geometry, stale running and completed results, replacement/deletion,
cross-sprite queue deferral, CPU/GPU overlap pressure, source/native failure,
empty artwork and teardown before a held capture finishes. The content check
compares an owned requested-resolution raster with the existing fresh asset
renderer, checks exact output/scratch limits, and preserves unsupported-feature
rejections. The runtime seam checks project generation cleanup.

The real GL quality diagnostic adds three SVG fixtures to the existing scalable
quality test: a non-square padded translucent/stroked viewport, a gradient
with a shifted viewBox, and a fractional viewport. It compares full worker/fresh GL RGBA and alpha output at
zero and 17.25-degree rotation and opacity 0.63. The independent bilinear color
reference keeps the existing channel tolerance of two and separately counts
the narrow float-boundary ambiguity policy. This quality check does not measure
complete-path frame pacing or compositor-visible cadence.

Run portable checks with `ctest --test-dir build/windows-release -C Release
-R '^(svg|anchor_presenter|runtime)$' --output-on-failure`. Native GL checks
require an exclusive supervised desktop run. The full 16.7 ms warm-frame,
50 ms input and 200 ms sharp acceptance matrix remains ticket #11 work. Mac
continues to use its supported compatibility adapter; Mac native/runtime SVG
behavior requires platform verification.

## Recorded validation

The final Release all-target build passed with parallelism four. The headless
suite passed 28 checks in 43.08 seconds, including the existing runtime test,
the SVG runtime generation check and the SVG/presenter regressions. Four
separately audited input/event/dialog/process boundary checks passed in
1.31 seconds. An earlier mixed extra-test run included hidden real GL in
`shaped_editor`, `ui_density` and `drawer_hidden` without the exclusive grant.
Those observations are excluded from authorized acceptance evidence.

The final supervised quality test passed in 0.69 seconds. All three SVG fixtures
matched fresh requested-resolution GL output byte for byte in RGBA and alpha
at both angles. Their independent normal and raw channel errors were at most
one under the unchanged tolerance of two; the shifted-viewBox gradient counted
two separately bounded numerical boundary pixels at 17.25 degrees. The existing
hybrid-text raw boundary difference of 35 remains separately reported under the
unchanged narrow float-edge policy.

The accompanying PNG regression failed its first premultiplied compositor
sample at `test_png_retained.c:75`: white 255,255,255 appeared where
255,191,159 was required. Its tolerance of three was unchanged. This supervised
suite therefore passed one of two tests, with input, cursor and exact saved
foreground HWND 525984 restored. The PNG check did not reach its GL error
injection in that run. The parent task retains this failure for separate
investigation before final specification acceptance. Earlier held-input refusal
and accepted-press/cursor-clip failure are also retained separately.

These records establish SVG source ownership, canonical geometry and requested
GL quality. They do not establish the full specification's native/compositor
or performance acceptance.
