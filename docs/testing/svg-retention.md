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
size, source padding or grabbed point. A fresh compatibility/preview request
may replace the shared asset's single raster; existing sprite CPU endpoints and
textures keep their own geometry and pixels. Two sprites sharing one SVG can
therefore retain different resolutions.

The CPU compatibility adapter and anchor previews also pass the canonical SVG
viewport to the mapped GL export. They compute rotated extents from that
viewport rather than rounded source pixels. A 13.5 by 7.5 SVG at 337 DIP and
17.25 degrees has a 378 by 279 pixel extent at density one; interpreting its
337 by 188 source pixels as the layout would incorrectly add a row. Native
Mac geometry remains unverified; the adapter builds and its public presenter
geometry check runs with a rendering boundary.

SVG requests use the existing presenter generation, instance, snapshot revision,
serial, endpoint size and density tags. Completion revalidates the live target.
Cancellation, coalescing, bounded admission, project retirement and final join
follow the same owner contracts as scalable text. Successful private work counts
once, including completed work discarded after cancellation. A failed generator
or rejected candidate preserves usable artwork. A broken native/GL publication
removes the failed sprite and releases input interception.

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
cross-sprite queue rejection, CPU/GPU overlap pressure, source/native failure,
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
