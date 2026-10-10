"""Rebuild the original MIT-licensed tiny TrueType authoring test fixture.
Developer helper only; requires fontTools. No runtime or test dependency.
"""
from pathlib import Path
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen


def glyph(name):
    pen = TTGlyphPen(None)
    if name in ("B", ".notdef"):
        pen.moveTo((0, 0))
        for point in [(600, 0), (600, 700), (0, 700)]:
            pen.lineTo(point)
        pen.closePath()
        if name == "B":
            for y in (100, 400):
                pen.moveTo((100, y))
                for point in [(100, y + 200), (500, y + 200), (500, y)]:
                    pen.lineTo(point)
                pen.closePath()
    elif name == "O":
        pen.moveTo((300, 0))
        for control, end in [((600, 0), (600, 350)), ((600, 700), (300, 700)),
                             ((0, 700), (0, 350)), ((0, 0), (300, 0))]:
            pen.qCurveTo(control, end)
        pen.closePath()
        pen.moveTo((300, 100))
        for control, end in [((100, 100), (100, 350)), ((100, 600), (300, 600)),
                             ((500, 600), (500, 350)), ((500, 100), (300, 100))]:
            pen.qCurveTo(control, end)
        pen.closePath()
    elif name == "A":
        pen.moveTo((0, 0))
        pen.lineTo((600, 0))
        pen.lineTo((300, 700))
        pen.closePath()
    return pen.glyph()


fb = FontBuilder(1000, isTTF=True)
order = [".notdef", "space", "A", "B", "O"]
fb.setupGlyphOrder(order)
fb.setupCharacterMap({32: "space", 65: "A", 66: "B", 79: "O", 233: "B"})
fb.setupGlyf({name: glyph(name) for name in order})
fb.setupHorizontalMetrics({name: (800, 0) for name in order})
fb.setupHorizontalHeader(ascent=800, descent=-200)
fb.setupNameTable({"familyName": "Agent CAD Authoring Test", "styleName": "Regular",
                   "uniqueFontIdentifier": "AgentCADAuthoringTest1", "fullName": "Agent CAD Authoring Test",
                   "psName": "AgentCADAuthoringTest", "version": "Version 1.000"})
fb.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
fb.setupPost()
fb.setupMaxp()
fb.font["head"].created = fb.font["head"].modified = 3786912000
fb.save(Path(__file__).with_name("authoring-test.ttf"))
