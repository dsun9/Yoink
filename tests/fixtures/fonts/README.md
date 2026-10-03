# Owned font fixtures

`PointyFixture.ttf` and `PointyFixture.ttc` are original geometric glyphs made
for PointyPointer tests, Copyright 2026 PointyPointer contributors, licensed
under the SIL Open Font License 1.1 (`OFL.txt`). They contain space, A, and an
acute combining mark, with no emoji. No glyph outlines came from another font.

`generate.py` is the complete provenance and reproduction source, generated
with fontTools 4.66.1. The collection contains Regular at face 0 and Wide at
face 1, with distinct family names and glyph widths to test face selection.
Metadata-reader restriction tests pass synthetic OS/2 fsType values directly
to the production restriction policy; no commercial font is modified.

`PointyOverhang.ttf` is generated from the same original outlines with an
original negative-bearing j and a raised acute mark. Its destination-size
native coverage provides deterministic overhang/accent clipping regressions
at small sizes without depending on an installed commercial typeface.

`PointyVariable.ttf` uses the same owned `Pointy Fixture` outlines and family
name as `PointyFixture.ttf`, with one weight axis and a named Bold instance at
weight 700. Its `gvar` table changes A's right-side points. It has one collection
face, but FreeType can successfully open encoded face value 65536 to select
named instance 1. Storage must reject that value as an out-of-range collection
index. `generate_variable.py` reproduces it using fontTools 4.66.1 without
rewriting the other fixtures. Copyright and OFL 1.1 terms are unchanged.
