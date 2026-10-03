"""Owned geometric OFL fixture; generated with fontTools 4.66.1.
No source glyph outlines are copied. Reproduce with python generate.py.
"""
from pathlib import Path
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTCollection

root = Path(__file__).parent
def build(family, wide=False, overhang=False):
    names = ['.notdef', 'space', 'A', 'acutecomb']
    if overhang:
        names.append('j')
    fb = FontBuilder(1000, isTTF=True)
    fb.setupGlyphOrder(names)
    cmap = {32:'space', 65:'A', 0x301:'acutecomb'}
    if overhang:
        cmap[0x6a] = 'j'
    fb.setupCharacterMap(cmap)
    glyphs = {}
    for name in names:
        pen = TTGlyphPen(None)
        if name == 'A':
            for pts in [[(50,0),(300,700),(550 if not wide else 850,0)], [(200,100),(300,440),(400,100)]]:
                pen.moveTo(pts[0])
                for p in pts[1:]: pen.lineTo(p)
                pen.closePath()
        elif name == 'j':
            points = [(-280,-260),(100,-260),(400,750),(80,750),(-50,30),(-280,30)]
            pen.moveTo(points[0])
            for point in points[1:]:
                pen.lineTo(point)
            pen.closePath()
        elif name == 'acutecomb' and overhang:
            pen.moveTo((-300,1050))
            pen.lineTo((100,1150))
            pen.lineTo((100,1190))
            pen.lineTo((-300,1090))
            pen.closePath()
        elif name in ('.notdef','acutecomb'):
            pen.moveTo((0,720 if name=='acutecomb' else 0))
            pen.lineTo((100,820 if name=='acutecomb' else 0))
            pen.lineTo((100,900 if name=='acutecomb' else 600))
            pen.lineTo((0,800 if name=='acutecomb' else 600))
            pen.closePath()
        glyphs[name] = pen.glyph()
    fb.setupGlyf(glyphs)
    metrics = {n:(0 if n=='acutecomb' else (900 if wide else 600),0) for n in names}
    if overhang:
        metrics['j'] = (200,-280)
        metrics['acutecomb'] = (0,-300)
    fb.setupHorizontalMetrics(metrics)
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
build('Pointy Overhang', overhang=True).save(root/'PointyOverhang.ttf')
