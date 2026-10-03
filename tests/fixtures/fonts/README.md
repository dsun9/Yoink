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
