# Approximate sprite grab area

Tracked by [issue #18](https://github.com/dsun9/Yoink/issues/18).

Status: accepted design, implemented and validated on Windows, 2026-10-12.
The user confirmed the bounded region quadtree refinement after answering the
behavior questions. Native performance measurements selected the limits below.
The [validation record](../../testing/sprite-grab-area.md) contains the measured
results, commands and remaining platform limitations.

## Settled behavior

- Approximate the visible silhouette and preserve sufficiently large transparent
  holes. A small amount of transparent space near visible edges may accept input.
- Use one grab area for left-click and drag, double-click, right-click deletion,
  and wheel rotation/resizing, for placed sprites and anchor previews.
- Preserve the separate small anchor activation target. An accepted drag stays
  captured through transparent areas and beyond the sprite until release or
  cancellation.
- Implement and validate Windows first. macOS is a follow-up and retains its
  current behavior until that work is implemented and validated.
- Bound normal source-space precision independently of displayed sprite size.
  A fixed coarse grid was selected initially; the user subsequently selected a
  bounded region quadtree to compress uniform blocks at fixed maximum depth.
- Use one union grab area for the whole animation. A location occupied in any
  frame remains in the union, even when the current frame leaves it transparent.
  Only holes absent from every frame are candidates for pass-through.
- If the chosen detail cannot meet the current performance budgets, reduce
  approximation detail within documented limits. Do not silently switch back
  to full-rectangle routing.
- Keep a nonempty animation union interactive during wholly invisible frames.
  Preserve accepted dragging across these frames. Opacity zero, explicit hiding,
  presentation failure, deletion and other cancellation still remove input.
  An animation with no visible artwork in any frame has an empty grab area.

## Verified starting point

Before this change, the specification deliberately required rectangular routing.
Windows direct GL surfaces accepted their full interactive native rectangle.
Compatibility presentation also explicitly enabled rectangular routing. See
`src/platform/windows/input.c`, `src/app/sprites.c`, and section 3 of
`docs/superpowers/specs/2026-10-03-pointypointer-design.md`.

The old CPU-frame renderer can read back both full-size color and coverage
planes. The retained production Windows path avoids these readbacks. Source
RGBA frames are available at acquisition/publication boundaries, so a cached
source occupancy approximation can avoid GPU readback. Building occupancy still
adds source scanning work; the existing visibility query stops at the first
nonzero alpha pixel.

The pinned SDL Windows shape builder stretches a supplied shape to window size,
scans every output pixel, and combines regions for occupied row spans. Passing
a small mask to that API does not bound its output-size work. A capped native
region builder therefore avoids this output-size scan.

Windows regions also clip visible drawing. Any approximation using a window
region must conservatively retain visible source alpha, filtering support and
transform rounding. Sampling only cell centers or discarding faint alpha can
remove visible artwork. See [Microsoft SetWindowRgn documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowrgn).
[SDL documents shape updates as expensive](https://wiki.libsdl.org/SDL3/SDL_SetWindowShape).

GIF import already decodes every full composited canvas, with disposal handled
by the decoder. The asset cache is limited to 256 MiB of RGBA plus alpha per
resource and 4096 frames. Union preparation therefore needs no second all-frame
cache. The implementation lazily scans those canvases once at first acquisition.
It stops scanning a cell after the first nonzero alpha in any frame and skips
remaining pixel scans once every cell is occupied. Tiny canvases use their actual
pixel dimensions for this scan. Compression follows the completed union.
Source preparation remains measured separately from decode, upload and native
publication; a small final shape does not make source scanning free.

The asset manager is single-threaded. The existing raster worker accepts owned
captures and frame-shaped results, and must not be given an owner asset pointer
as an undocumented shortcut. Any later background union preparation needs an
explicit lifetime and result contract.

## Accepted quadtree refinement

The user asked whether a quadtree or BVH would be better. A bounded region
quadtree is the accepted refinement. It divides
mixed occupancy into quadrants, represents uniform empty/occupied blocks with
one leaf, and stops refinement at a fixed source-space depth. See the
[University of Maryland region quadtree description](https://donar.umiacs.umd.edu/quadtree/regions/regionquad.html).

This retains the chosen bounded precision policy while allowing larger blocks
inside uniform areas. At the depth limit, a mixed block containing visible alpha
is occupied. Coarsening must expand occupancy, never discard visible artwork.
Quadtree compression preserves the finest occupancy approximation; it does not
by itself improve precision or prove low native update costs.

A BVH could speed queries among already prepared shapes, but the current
bottleneck to validate is construction and native publication of a pass-through
region. A CPU spatial query alone does not change OS mouse routing. No BVH is
proposed unless native evidence establishes a separate need for it.

Flattening quadtree leaves into native region data must preserve holes and
produce properly ordered, nonoverlapping rectangles. Windows
[RGNDATA](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-rgndata)
and [ExtCreateRegion](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-extcreateregion)
provide the native construction path. Projection performs rotation before the
native rectangle list is built, with filtering, rounding and angle margins.

Translation reuses the same local shape. Frame selection reuses the animation
union. Rotation, size, density, canonical mapping or accepted source replacement
may update the local native region. The source/shape ownership must commit or
roll back with the corresponding accepted artwork.

## Selected limits and ownership

- Source depth 7, with at most 128 cells per axis and 21,845 four-byte nodes.
  Uniform occupied or empty areas need one node.
- Final output uses 64 cells per axis and at most 1,024 native rectangles.
  If that list exceeds the cap, conservative 32-cell output bounds the list to
  at most 512 runs. Coarsening only adds occupied space.
- Input angle uses 0.05-degree buckets. An angular envelope contains the actual
  continuous visual angle. Filtering adds half a source texel within the source
  rectangle; rounding adds two native pixels.
- Source trees and cached native rectangle arrays share a separate 8 MiB budget.
  Admission includes accepted/candidate overlap and happens before allocation.
  The last tree reference refunds its bytes. Replacement rejection preserves the
  accepted snapshot, artwork and grab area.
- Translation and GIF frame changes reuse the accepted shape. Other geometry
  changes reproject it. Identical rectangle lists skip native region publication.
  `SetWindowRgn` uses no separate paint invalidation because GL drawing follows
  immediately. The native adapter takes ownership of successfully applied regions.

At 96, 384 and 1536 logical pixels, an unrotated square's normal output cells are
about 1.5, 6 and 24 logical pixels wide. The fallback cells are twice those sizes.
These numbers describe output cells, not total edge tolerance. Source occupancy,
filtering, rounding and rotation add tolerance, and rotation enlarges the output
extent. Tiny gaps may remain interactive. Large holes survive where this bounded
approximation leaves empty cells. There is no explicit rectangle fallback, but a
dense fragmented source can naturally occupy every cell at the chosen precision.

The routing specification and native tests now reflect the accepted behavior.
Existing rendering, input-response and sharp-publication budgets remain the
baseline. macOS keeps its prior adapter and needs a separately validated follow-up.
