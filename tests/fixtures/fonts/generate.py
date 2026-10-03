"""Owned geometric OFL fixture; generated with fontTools 4.66.1.
No source glyph outlines are copied. Reproduce with python generate.py.
"""
from pathlib import Path
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTCollection

root = Path(__file__).parent
def build(family, wide=False):
    names = ['.notdef', 'space', 'A', 'acutecomb']
    fb = FontBuilder(1000, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap({32:'space', 65:'A', 0x301:'acutecomb'})
    glyphs = {}
    for name in names:
        pen = TTGlyphPen(None)
        if name == 'A':
            for pts in [[(50,0),(300,700),(550 if not wide else 850,0)], [(200,100),(300,440),(400,100)]]:
                pen.moveTo(pts[0])
                for p in pts[1:]: pen.lineTo(p)
                pen.closePath()
        elif name in ('.notdef','acutecomb'):
            pen.moveTo((0,720 if name=='acutecomb' else 0))
            pen.lineTo((100,820 if name=='acutecomb' else 0))
            pen.lineTo((100,900 if name=='acutecomb' else 600))
            pen.lineTo((0,800 if name=='acutecomb' else 600))
            pen.closePath()
        glyphs[name] = pen.glyph()
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics({n:(0 if n=='acutecomb' else (900 if wide else 600),0) for n in names})
    fb.setupHorizontalHeader(ascent=950,descent=-200)
    fb.setupNameTable({'familyName':family,'styleName':'Regular','uniqueFontIdentifier':family,
                      'fullName':family,'psName':family.replace(' ',''),
                      'copyright':'Copyright 2026 PointyPointer contributors',
                      'licenseDescription':'SIL Open Font License 1.1'})
    fb.setupOS2(sTypoAscender=950,sTypoDescender=-200,usWinAscent=950,usWinDescent=200,fsType=0)
    fb.setupPost()
    fb.setupMaxp()
    fb.font['head'].created = fb.font['head'].modified = 3800000000
    return fb.font

regular=build('Pointy Fixture')
regular.save(root/'PointyFixture.ttf')
collection=TTCollection()
collection.fonts=[regular,build('Pointy Fixture Wide',True)]
collection.save(root/'PointyFixture.ttc')
