# S4 safety sweep v11.04 (static, read-only, not compiled)
Scope: images.c (GIF/BMP/ICO + lib/image/png.h), glue/http.c, websock.c, web/text/ttf.c + font.c, web/paint/svg.c.
Not covered: lib/image/jpeg.h, webp_decode (ran out of time). woff2.c/imgx.c skipped (C1).

## 1. HIGH - NaN coordinate -> wild heap write in rasterizer (web/text/font.c:145 nwe_acc_line, reached from web/paint/svg.c:182)
parse_float("1e99") returns +inf (web/core/util.c:110). mapp() computes m.a*x + m.c*y + m.e; with identity m.c=0, 0*inf = NaN,
so the point becomes NaN. fill_poly's bbox ignores NaN (comparisons false), w/h stay small, then nwe_acc_line gets NaN:
(int)floorf(NaN) = INT_MIN on x86 -> xai=INT_MIN (clamps only check the upper side) -> acc[ls + xai] += ... writes ~8 GB before acc.
If y0 is NaN, ys=(int)NaN=INT_MIN and the loop runs ~2^31 rows with wrapping ls.
Page: `<svg width=100 height=100><path d="M0 0 L10 10 L1e99 5 Z"/></svg>` -> engine crash / heap corruption.
Patch (font.c, first line of nwe_acc_line):
    if (!(x0 > -1e7f && x0 < 1e7f && x1 > -1e7f && x1 < 1e7f && y0 > -1e7f && y0 < 1e7f && y1 > -1e7f && y1 < 1e7f)) return;
(Also catches inf. Check the build has no -ffast-math, or NaN compares may be folded away.) Optional belt: in svg.c padd2 drop non-finite points the same way.

## 2. HIGH - PNG chunk length overflow -> OOB read (lib/image/png.h:198)
`if(clen<0||p+12+clen>len)break;` - p+12+clen overflows int when clen is near 0x7FFFFFFF, so the check passes. IDAT then copies
data[0..] up to outcap (24 MB) past the image buffer; `p+=12+clen` goes negative and the loop reads png[negative].
Page: <img src=evil.png> with an IDAT chunk length 0x7FFFFFF0.
Patch: `if(clen<0||clen>len-p-12)break;`  (also: IHDR with clen<13 reads past the chunk -> add `if(clen<13)return -2;` in the IHDR branch.)

## 3. HIGH - PNG width overflow -> OOB read past raw buffer (lib/image/png.h:205,219,241,253-275)
w/h are unbounded ints. w=0x40000001 makes w*bpp_bits wrap, so stride=1 and need=h*(1+stride) passes the rawcap check; the
downscale loop then reads cur[x*8] for x up to ~w/f (RGBA16 case) far beyond the 64 MB raw buffer -> page fault. need is also int.
Patch: in IHDR branch after reading w,h: `if(w<=0||h<=0||w>16384||h>16384)return -2;` and line 241: `long need=(long)h*(1+stride); if(need>rawcap)return -7;`
(the interlaced `need` at :222 -> long as well).

## 4. HIGH - truetype outline loses its terminator -> OOB read (web/text/ttf.c:337-345 ttf_outline)
sp() silently drops values once v.n reaches cap 59990, so `sp(&v,5)` is dropped too and an op can be cut mid-operands.
font.c:249/268 loops `while(*p != 5)` over the nalloc'd copy -> reads past the heap block until a 5 appears.
Font: composite glyph referencing a 4000-point glyph 64 times (> 60000 shorts). Any @font-face page.
Patch (ttf_outline, replace `sp(&v, 5);`):
    { int q = 0; while (q < v.n) { int op = buf[q]; int k = op <= 1 ? 2 : op == 2 ? 4 : op == 3 ? 6 : op == 4 ? 0 : -1; if (k < 0 || q + 1 + k > v.n) break; q += 1 + k; } v.n = q; buf[v.n++] = 5; }
(buf is 60000, cap 59990 so there is room.)

## 5. MEDIUM - exponential composite-glyph recursion = hang (web/text/ttf.c:216-241 outline)
depth<=6 and guard<64 per level -> up to 64^6 (~7e10) outline() calls for one glyph that composes itself 64 times.
Patch: `static int g_ol_budget;` ; at top of outline(): `if (--g_ol_budget < 0) return;` ; in ttf_outline before the call: `g_ol_budget = 4096;`

## 6. MEDIUM - SVG <use> billion-laughs = hang (web/paint/svg.c:525-541 draw_el)
node_contains() only blocks direct self-reference. Chain g1..g30 where each gN holds 10 `<use href="#g(N-1)">` -> 10^30 draws (depth cap 30).
Patch: `static int g_svg_budget;` ; draw_el first line: `if (--g_svg_budget < 0) return;` ; svg_paint before draw_el: `g_svg_budget = 50000;`

## 7. MEDIUM - cmap scan hang (web/text/ttf.c:120-126 fmt12, :100-115 fmt4)
Loops only stop at n<cap, but n grows only for valid glyphs. fmt12 groups with a=0,b=0x10FFFF,g=0x80000000 never add -> 1.1M iterations per
group x ~10k groups in 120 KB = ~1e10 iterations. fmt4 with nglyphs=1: 32k segments x 65k = 2e9.
Patch: add `long work = 0;` before the format branches, and in both inner loops `if (++work > 4000000) break;` (break the outer too:
use `goto done;` with `done:` before `#undef ADDG`).

## 8. MEDIUM - BMP negative pixel offset -> OOB read (apps/internet/browser/images.c:118,127)
off is a signed int from the file; only the row's end is checked against d+len. off=0x80000000 -> row points before d.
Patch after line 121: `if(off<54||off>len)return -1;`

## 9. MEDIUM - ICO entry offset+size overflow -> OOB read (images.c:196-198)
`off>0&&off+sz<=len` overflows (off=0x7FFFFF00, sz=0x200). Also sz 9..15 lets p[8..15] read past the entry.
Patch line 197: `if(off>0&&sz>=40&&off<=len&&sz<=len-off){`  (ICO path is the favicon loader -> any site).

## 10. LOW - http.c chunk-size int overflow (apps/internet/browser/glue/http.c:5)
Check runs after `sz = sz*16+v`; sz=0x10000000 passes, next digit computes 2^32 (signed overflow UB, wraps to 0..15) -> framing desync,
not memory-unsafe (j+sz+2>n bounds it). Patch: move check before the multiply: `if (sz > (1 << 27)) return -1; sz = sz * 16 + v;`
Content-Length (:77) `L*10` can overflow long with 20+ digits; patch: `while(... && L < 100000000000L)`.

## Checked, no bug found
- websock.c w_parse/rx_grow/w_frame: lengths capped by WS_RXMAX before use, u32 math fine. (Ping echo is not limited to 125 bytes - spec nit only.)
- GIF decoder: tables, stack (4100 vs max 4098 pushes), color index, and frame clipping are all bounded.
- dechunk (http.c:128), hdr_get, ttf find_tables/glyph_range/GSUB/ttf_shape bounds OK. woff1_to_sfnt OK (olen wrap only reaches inflate with negative cap -> fails).
- cmap fmt4/12 header reads up to 12 bytes past a short subtable (OOB read inside the font buffer only) - trivial, fix with `if (off + 16 > len) continue;`.
