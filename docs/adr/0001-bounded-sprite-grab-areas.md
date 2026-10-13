# Bound sprite grab areas and reuse animation unions

For [issue #18](https://github.com/dsun9/Yoink/issues/18), Windows sprites and
anchor previews use conservative region quadtrees with fixed source-space depth
and a cap on the final native rectangles after rotation. Full-frame GPU readback
and unrestricted native shape complexity would compromise the retained renderer's
response budgets. Animation input uses the union of all composited frames,
including blank frames and finite playback's final state, trading current-frame
precision for stable input and avoiding shape publication on frame changes.

Depth 7 bounds the source tree. Final projection uses 64 output cells per axis,
with conservative coarsening to 32 and a cap of 1,024 native rectangles. These
limits apply after rotation because the prototype's native transform multiplied
1,024 input rectangles into 9,227 native rectangles. Trees and cached rectangle
arrays share an 8 MiB admission budget including replacement overlap. See the
[accepted design](../superpowers/specs/2026-10-12-approximate-grab-area.md) and
[validation record](../testing/sprite-grab-area.md) for tolerances and measurements.
