These fixtures are original test data authored for PointyPointer (2026).
`generate.py` uses only Python's standard library and contains no third-party
artwork. PNGs exercise alpha normalization and the global cache budget. GIFs
use a four-color palette, tiny literal LZW streams, transparent index zero,
explicit disposal methods, and a NETSCAPE loop extension. The finite fixture
requests one repeat (two complete plays). The changing-alpha fixture alternates
an opaque three-pixel row with a transparent row for native input testing.
`retained-blue.png` is an opaque 8 by 4 blue rectangle for immutable PNG
replacement, canonical aspect and retained GPU accounting tests.
`retained-empty.png` has zero visual alpha at every pixel to test invisible
source cleanup independently of the sprite's pose opacity.
`menu-triangle.svg` is an original red triangle used by the Settings media
chooser's PNG/GIF/SVG save and reopen acceptance flow.
