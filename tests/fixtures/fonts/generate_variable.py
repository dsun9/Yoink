"""Create an owned single-face variable font with a real named instance.

Uses only PointyFixture.ttf's original geometric outlines (OFL 1.1).
Reproduce with fontTools 4.66.1: python generate_variable.py.
"""
from pathlib import Path
from fontTools.fontBuilder import FontBuilder
from fontTools.ttLib import TTFont
from fontTools.ttLib.tables.TupleVariation import TupleVariation

root = Path(__file__).parent
font = TTFont(root / "PointyFixture.ttf", recalcTimestamp=False)
builder = FontBuilder(font=font)
builder.setupFvar(
    [("wght", 100, 400, 900, "Weight")],
    [{"location": {"wght": 700}, "stylename": "Bold"}],
)
variations = {name: [] for name in font.getGlyphOrder()}
coordinates, _, _ = font["glyf"]["A"].getCoordinates(font["glyf"])
deltas = [(20 if x > 300 else 0, 0) for x, _ in coordinates]
deltas.extend([(0, 0)] * 4)  # Four TrueType phantom points.
variations["A"] = [TupleVariation({"wght": (0, 1, 1)}, deltas)]
builder.setupGvar(variations)
font.save(root / "PointyVariable.ttf")
