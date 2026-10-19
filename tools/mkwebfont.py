import sys, os
from fontTools.ttLib import TTFont
from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.pens.transformPen import TransformPen

D = '/usr/share/fonts/truetype/liberation/'
FACES = [
    ('sans', 'LiberationSans-Regular.ttf'),
    ('sans_b', 'LiberationSans-Bold.ttf'),
    ('sans_i', 'LiberationSans-Italic.ttf'),
    ('sans_bi', 'LiberationSans-BoldItalic.ttf'),
    ('serif', 'LiberationSerif-Regular.ttf'),
    ('serif_b', 'LiberationSerif-Bold.ttf'),
    ('serif_i', 'LiberationSerif-Italic.ttf'),
    ('mono', 'LiberationMono-Regular.ttf'),
    ('mono_b', 'LiberationMono-Bold.ttf'),
]
CPS = list(range(32, 127)) + list(range(160, 384)) + [
    0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E, 0x2020, 0x2021, 0x2022, 0x2026,
    0x2030, 0x2032, 0x2033, 0x2039, 0x203A, 0x20AC, 0x2122, 0x2190, 0x2191, 0x2192, 0x2193, 0x2194,
    0x2212, 0x221E, 0x2248, 0x2260, 0x2264, 0x2265, 0x25B2, 0x25BC, 0x25CF, 0x25A0, 0x2605, 0x2713,
    0x0391, 0x0392, 0x0393, 0x0394, 0x03A3, 0x03A9, 0x03B1, 0x03B2, 0x03B3, 0x03B4, 0x03BB, 0x03BC, 0x03C0, 0x03C3,
    0x0401, 0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C,
    0x041D, 0x041E, 0x041F, 0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A,
    0x042B, 0x042C, 0x042D, 0x042E, 0x042F] + list(range(0x0430, 0x0450)) + [0x0451]

SYM = [0x2190, 0x2191, 0x2192, 0x2193, 0x2194, 0x2195, 0x21A9, 0x21AA, 0x21BB, 0x21D0, 0x21D2, 0x21D4, 0x2713, 0x2714, 0x2715, 0x2716, 0x2717, 0x2718,
    0x2605, 0x2606, 0x2660, 0x2663, 0x2665, 0x2666, 0x2630, 0x22EE, 0x22EF, 0x25B6, 0x25C0, 0x25B2, 0x25BC, 0x25B8, 0x25BE, 0x25CB, 0x25C6, 0x25A1,
    0x2610, 0x2611, 0x2612, 0x2318, 0x23CE, 0x2709, 0x260E, 0x2699, 0x26A0, 0x2600, 0x2601, 0x266A, 0x221A, 0x2211, 0x2202, 0x00D7, 0x2022, 0x2023,
    0x2027, 0x2043, 0x2044, 0x203B, 0x2116, 0x2103, 0x2109, 0x2153, 0x2154, 0x215B, 0x2160, 0x2161, 0x2162, 0x25CF, 0x25A0, 0x2302, 0x2328, 0x23F0,
    0x2B50, 0x2764, 0x279C, 0x27A4, 0x2794, 0x2B06, 0x2B07, 0x2B05, 0x27A1]
CPS = sorted(set(CPS + SYM))
FALLBACK = {'sans': '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 'sans_b': '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'}
SMALL = [c for c in CPS if (c < 0x100 or c >= 0x2000) and c not in SYM]

def face(name, path):
    f = TTFont(path)
    gs = f.getGlyphSet()
    cmap = f.getBestCmap()
    upm = f['head'].unitsPerEm
    hh = f['hhea']
    os2 = f['OS/2']
    asc, desc, gap = hh.ascent, -hh.descent, hh.lineGap
    if getattr(os2, 'usWinAscent', 0):
        asc, desc, gap = os2.usWinAscent, os2.usWinDescent, max(0, hh.ascent - hh.descent + hh.lineGap - os2.usWinAscent - os2.usWinDescent)
    hmtx = f['hmtx']
    data = []
    glyphs = []
    fbf = TTFont(FALLBACK[name]) if name in FALLBACK and os.path.exists(FALLBACK[name]) else None
    fbgs = fbf.getGlyphSet() if fbf else None
    fbcmap = fbf.getBestCmap() if fbf else {}
    fbsc = upm / fbf['head'].unitsPerEm if fbf else 1
    for cp in (CPS if name in ('sans', 'sans_b') else SMALL):
        gn = cmap.get(cp)
        if gn is None:
            fgn = fbcmap.get(cp)
            if fgn is None:
                continue
            adv = round(fbf['hmtx'][fgn][0] * fbsc)
            rp = DecomposingRecordingPen(fbgs)
            fbgs[fgn].draw(TransformPen(rp, (fbsc, 0, 0, fbsc, 0, 0)))
        else:
            adv = hmtx[gn][0]
            rp = DecomposingRecordingPen(gs)
            gs[gn].draw(rp)
        off = len(data)
        for op, args in rp.value:
            if op == 'moveTo':
                data += [0, round(args[0][0]), round(args[0][1])]
            elif op == 'lineTo':
                data += [1, round(args[0][0]), round(args[0][1])]
            elif op == 'qCurveTo':
                pts = list(args)
                if pts[-1] is None:
                    pts = pts[:-1]
                    first_on = ((pts[-1][0] + pts[0][0]) / 2, (pts[-1][1] + pts[0][1]) / 2)
                    data += [0, round(first_on[0]), round(first_on[1])]
                    pts = pts + [first_on]
                for i in range(len(pts) - 1):
                    c = pts[i]
                    e = pts[i + 1] if i == len(pts) - 2 else ((pts[i][0] + pts[i + 1][0]) / 2, (pts[i][1] + pts[i + 1][1]) / 2)
                    data += [2, round(c[0]), round(c[1]), round(e[0]), round(e[1])]
            elif op == 'curveTo':
                p = args
                data += [3, round(p[0][0]), round(p[0][1]), round(p[1][0]), round(p[1][1]), round(p[2][0]), round(p[2][1])]
            elif op in ('closePath', 'endPath'):
                data += [4]
        data += [5]
        glyphs.append((cp, adv, off))
    glyphs.sort()
    return dict(name=name, upm=upm, asc=asc, desc=desc, gap=gap, data=data, glyphs=glyphs)

def emit(out):
    fs = [face(n, D + p) for n, p in FACES]
    o = ['#include "web/nwe.h"']
    for fc in fs:
        n = fc['name']
        o.append('static const short fd_%s[%d]={%s};' % (n, len(fc['data']), ','.join(map(str, fc['data']))))
        o.append('static const nwe_glyphrec fg_%s[%d]={%s};' % (n, len(fc['glyphs']), ','.join('{%d,%d,%d}' % g for g in fc['glyphs'])))
    o.append('const nwe_facerec nwe_faces[%d]={' % len(fs))
    for fc in fs:
        n = fc['name']
        o.append('{"%s",%d,%d,%d,%d,%d,fg_%s,fd_%s},' % (n, fc['upm'], fc['asc'], fc['desc'], fc['gap'], len(fc['glyphs']), n, n))
    o.append('};')
    o.append('const int nwe_nfaces=%d;' % len(fs))
    open(out, 'w').write('\n'.join(o) + '\n')
    print('faces', len(fs), 'shorts', sum(len(f['data']) for f in fs), 'glyphs', sum(len(f['glyphs']) for f in fs))

emit(sys.argv[1])
