"""Original geometric outline + alpha bitmap fixture, OFL 1.1.

Run with fontTools 4.66.1. No artwork or font bytes are copied.
"""
from pathlib import Path
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import newTable
from fontTools.ttLib.tables.E_B_L_C_ import Strike, SbitLineMetrics, eblc_index_sub_table_1
from fontTools.ttLib.tables.E_B_D_T_ import ebdt_bitmap_format_1
from fontTools.ttLib.tables.BitmapGlyphMetrics import SmallGlyphMetrics

root = Path(__file__).parent
names = [".notdef", "space", "A", "box"]
builder = FontBuilder(1000, isTTF=True)
builder.setupGlyphOrder(names)
builder.setupCharacterMap({32: "space", 65: "A", 66: "box"})
glyphs = {}
for name in names:
    pen = TTGlyphPen(None)
    if name in ("A", ".notdef"):
        pen.moveTo((50, 0))
        pen.lineTo((300, 700))
        pen.lineTo((550, 0))
        pen.closePath()
    glyphs[name] = pen.glyph()
builder.setupGlyf(glyphs)
builder.setupHorizontalMetrics({name: (1000 if name == "box" else 600, 0) for name in names})
builder.setupHorizontalHeader(ascent=1000, descent=-200)
builder.setupNameTable({"familyName": "Pointy Hybrid", "styleName": "Regular",
    "fullName": "Pointy Hybrid", "psName": "PointyHybrid", "uniqueFontIdentifier": "PointyPointer original hybrid fixture",
    "copyright": "Copyright 2026 PointyPointer contributors", "licenseDescription": "SIL Open Font License 1.1"})
builder.setupOS2(sTypoAscender=1000, sTypoDescender=-200, usWinAscent=1000, usWinDescent=200, fsType=0)
builder.setupPost()
builder.setupMaxp()
font = builder.font
font["head"].created = font["head"].modified = 3800000000

# A 16-pixel alpha box with alpha-128 border and transparent center.
rows = bytearray()
for y in range(16):
    for x in range(16):
        alpha = 0 if 6 <= x < 10 and 6 <= y < 10 else 128 if x in (0, 15) or y in (0, 15) else 255
        rows.append(alpha)
glyph = ebdt_bitmap_format_1(None, None)
glyph.metrics = SmallGlyphMetrics()
for name, value in {"height": 16, "width": 16, "BearingX": 0, "BearingY": 16, "Advance": 16}.items():
    setattr(glyph.metrics, name, value)
glyph.imageData = bytes(rows)
data = newTable("EBDT")
data.version = 2.0
data.strikeData = [{"box": glyph}]
font["EBDT"] = data
location = newTable("EBLC")
location.version = 2.0
strike = Strike()
table = strike.bitmapSizeTable
table.colorRef = 0
table.ppemX = table.ppemY = 16
table.bitDepth = 8
table.flags = 1
table.hori = SbitLineMetrics()
table.vert = SbitLineMetrics()
for metric in (table.hori, table.vert):
    for name in ("ascender", "descender", "widthMax", "caretSlopeNumerator", "caretSlopeDenominator", "caretOffset", "minOriginSB", "minAdvanceSB", "maxBeforeBL", "minAfterBL", "pad1", "pad2"):
        setattr(metric, name, {"ascender": 16, "widthMax": 16, "caretSlopeNumerator": 1, "maxBeforeBL": 16}.get(name, 0))
subtable = eblc_index_sub_table_1(None, None)
subtable.indexFormat = 1
subtable.imageFormat = 1
subtable.names = ["box"]
strike.indexSubTables = [subtable]
location.strikes = [strike]
font["EBLC"] = location
font.save(root / "PointyHybrid.ttf")
