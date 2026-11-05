import sys
from PIL import Image, ImageDraw, ImageFont
D = '/usr/share/fonts/opentype/inter/'
SIZES = [('Inter-Regular.otf', 13), ('Inter-Regular.otf', 15), ('Inter-Medium.otf', 15), ('Inter-Regular.otf', 18),
         ('Inter-SemiBold.otf', 24), ('Inter-SemiBold.otf', 26), ('Inter-SemiBold.otf', 40), ('Inter-Light.otf', 84)]
out_c, out_h = sys.argv[1], sys.argv[2]
data = bytearray(); glyphs = []; fonts = []
for fi, (fn, px) in enumerate(SIZES):
    f = ImageFont.truetype(D + fn, px)
    asc, desc = f.getmetrics()
    first = len(glyphs)
    for ch in range(32, 127):
        c = chr(ch)
        adv = int(round(f.getlength(c)))
        box = f.getbbox(c)
        if box[2] <= box[0] or box[3] <= box[1]:
            glyphs.append((adv, 0, 0, 0, 0, len(data))); continue
        w, h = box[2] - box[0], box[3] - box[1]
        im = Image.new('L', (w, h), 0)
        ImageDraw.Draw(im).text((-box[0], -box[1]), c, font=f, fill=255)
        glyphs.append((adv, w, h, box[0], box[1], len(data)))
        data += im.tobytes()
    fonts.append((px, asc, desc, first))
with open(out_c, 'w') as o:
    o.write('#include "include/nvx/assets/uifont.h"\n')
    o.write('const unsigned char uf_data[%d]={' % len(data))
    o.write(','.join(str(b) for b in data)); o.write('};\n')
    o.write('const uf_glyph uf_glyphs[%d]={' % len(glyphs))
    o.write(','.join('{%d,%d,%d,%d,%d,%d}' % g for g in glyphs)); o.write('};\n')
    o.write('const uf_font uf_fonts[%d]={' % len(fonts))
    o.write(','.join('{%d,%d,%d,%d}' % f for f in fonts)); o.write('};\n')
with open(out_h, 'w') as o:
    o.write('#ifndef NVX_ASSETS_UIFONT_H\n#define NVX_ASSETS_UIFONT_H\n')
    o.write('typedef struct { short adv, w, h, x, y; int off; } uf_glyph;\n')
    o.write('typedef struct { short px, asc, desc; int first; } uf_font;\n')
    o.write('enum { UF_13, UF_15, UF_15M, UF_18, UF_24B, UF_26B, UF_40B, UF_84L, UF_N };\n')
    o.write('extern const unsigned char uf_data[];\nextern const uf_glyph uf_glyphs[];\nextern const uf_font uf_fonts[];\n')
    o.write('int uf_width(int f,const char* s);\nint uf_draw(int x,int y,int f,const char* s,unsigned int c);\nvoid uf_draw_ctr(int cx,int y,int f,const char* s,unsigned int c);\nint uf_cap_mid(int f);\n#endif\n')
print(len(data), 'bytes of glyphs')
