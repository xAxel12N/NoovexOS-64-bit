import sys, io, struct
from fontTools.ttLib import TTFont
from PIL import Image
SRC = sys.argv[1] if len(sys.argv) > 1 else '/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'assets/emoji32.bin'
SZ = 32
RANGES = [(0x2190, 0x21FF), (0x2300, 0x23FF), (0x2460, 0x24FF), (0x25A0, 0x27BF), (0x2900, 0x297F), (0x2B00, 0x2BFF), (0x3030, 0x3030), (0x303D, 0x303D), (0x3297, 0x3299), (0x1F000, 0x1FAFF)]
f = TTFont(SRC)
cmap = f.getBestCmap()
strike = f['CBDT'].strikeData[0]
ents = []
for cp in sorted(cmap):
    if not any(a <= cp <= b for a, b in RANGES):
        continue
    gn = cmap[cp]
    bm = strike.get(gn)
    if bm is None or not getattr(bm, 'imageData', None):
        continue
    im = Image.open(io.BytesIO(bm.imageData)).convert('RGBA')
    w, h = im.size
    s = SZ / max(w, h)
    im = im.resize((max(1, round(w * s)), max(1, round(h * s))), Image.LANCZOS)
    c = Image.new('RGBA', (SZ, SZ), (0, 0, 0, 0))
    c.paste(im, ((SZ - im.size[0]) // 2, (SZ - im.size[1]) // 2))
    b = io.BytesIO()
    q = c.quantize(colors=128, method=Image.Quantize.FASTOCTREE)
    q.save(b, 'PNG', optimize=True)
    ents.append((cp, b.getvalue()))
hdr = b'NVEM' + struct.pack('<II', len(ents), SZ)
tab = b''
data = b''
off = 0
for cp, png in ents:
    tab += struct.pack('<III', cp, off, len(png))
    data += png
    off += len(png)
open(OUT, 'wb').write(hdr + tab + data)
print(len(ents), 'emoji', len(hdr + tab + data), 'bytes')
