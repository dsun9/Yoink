"""Original geometric OFL fixture for installed-font boundary tests.

The two family names are synthetic lookup inputs. No Windows glyphs or font
bytes are copied. The first face has Latin A; the second has a colored box
mapped to the pointing hand. Run with fontTools 4.66.1.
"""
from pathlib import Path
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTCollection
from fontTools.colorLib.builder import buildCOLR, buildCPAL

root = Path(__file__).parent

def build(family, emoji):
    names = [".notdef", "space", "A", "hand", "hand_layer"]
    fb = FontBuilder(1000, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap({32: "space", **({0x1f449: "hand"} if emoji else {65: "A"})})
    glyphs = {}
    for name in names:
        pen = TTGlyphPen(None)
        if name not in ("space", "hand"):
            pen.moveTo((40, 0))
            pen.lineTo((500, 0))
            pen.lineTo((500, 700))
            pen.lineTo((40, 700))
            pen.closePath()
        glyphs[name] = pen.glyph()
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics({name: (600, 0) for name in names})
    fb.setupHorizontalHeader(ascent=900, descent=-200)
    fb.setupNameTable({
        "familyName": family, "styleName": "Regular", "fullName": family,
        "uniqueFontIdentifier": "PointyPointer synthetic " + family,
        "psName": "PointySynthetic" + family.replace(" ", ""),
        "version": "Version 7.25; PointyPointer original fixture",
        "copyright": "Copyright 2026 PointyPointer contributors",
        "licenseDescription": "SIL Open Font License 1.1",
        "licenseInfoURL": "https://openfontlicense.org/",
    })
    fb.setupOS2(sTypoAscender=900, sTypoDescender=-200, usWinAscent=900, usWinDescent=200, fsType=8)
    fb.setupPost()
    fb.setupMaxp()
    fb.font["head"].created = fb.font["head"].modified = 3800000000
    # The text face's hand is unencoded. Font capabilities must still report
    # its layers, as shaped alternates need not have their own cmap entries.
    fb.font["COLR"] = buildCOLR({"hand": [("hand_layer", 0)]}, version=0)
    fb.font["CPAL"] = buildCPAL([[(1.0, 0.6, 0.0, 1.0)]])
    return fb.font

collection = TTCollection()
collection.fonts = [build("Aptos", False), build("Segoe UI Emoji", True)]
collection.save(root / "PointyDefaultFonts.ttc")
