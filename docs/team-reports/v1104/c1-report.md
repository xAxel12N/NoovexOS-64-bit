# c1 report (v11.04) — web engine safety fixes, branch w-c1

Commit on w-c1: web/text/woff2.c, web/image/imgx.c
- H1 woff2_to_sfnt: each tlen is checked against (64 MiB - sum) BEFORE it is added, so the sum cannot wrap. The comp bound is rewritten without wrap (comp > len || r.i > len - comp). outsz is computed in 64-bit, with a 64 MiB cap per table and a 256 MiB total cap.
- H2 item_data: method/src are resolved once. Every extent is checked (off <= lim, len <= lim - off) and the running total is overflow-checked against 256 MiB, all before malloc. The copy loop no longer checks.
- M2 IX_MAXPX 16384^2 -> 8192^2 (64 MPix, max 256 MiB ARGB). It applies to the AVIF primary item, the dav1d frame and JXL. A lower cap (4096^2) would be safer for the kernel heap; that is a lead decision.
- M5 rebuild_hmtx gets nxm (the glyph count from glyf) and reads xm[i] only when i < nxm, otherwise 0.
- L3 parse_iloc: iloc v2 requires at least 10 body bytes.

Compile check: both files compiled with build.sh's WEBF flags (-msse, build/musl includes) plus -Wall -Wextra, with no diagnostics in either file. (ccheck.sh cannot compile imgx.c because j40 needs SSE; that fails on the base code too.)
Not run: build.sh and check.py.

Test: tools/webhost.c is a network harness with no direct decoder entry, so no repo test was added. I ran a hosted ASan test in /tmp/c1t/t.c, which #includes woff2.c with stub nalloc/brotli and uses a 2-table font with tlen 0xFFFFFFF0 + 0x20. The old code gives an ASan heap-buffer-overflow. The new code returns -1, and a sane 8+12 byte font still gives rc=0 with outlen=64.
Suggested AVIF tests (not run): iloc with 2 extents (len 20, len 2^64-10) should make item_data return 0. An ispe of 16384x16384 should be rejected. An iloc v2 box with an 8-byte body should be ignored.
Not fixed: M1 (brotli leak).

## Round 2 (commit on w-c1, after d7670b35)
- s4 #1: nwe_acc_line (web/text/font.c) returns early when any coordinate is NaN, inf or |v| >= 1e7. svg.c fill_poly also drops the whole polygon when any point is like that. Both use NaN-safe comparisons; WEBF has no -ffast-math.
- s4 #2/#3: lib/image/png.h now uses `clen>len-p-12`, requires IHDR clen>=13, rejects w,h outside 1..16384 and w*h>64M, and computes need as long (interlaced and normal paths).
- s4 #4: ttf_outline walks the op stream, cuts off a trailing op that is missing operands, and always appends 5.
- s4 #8/#9: images.c BMP requires off to be 54..len. ICO requires off>0, sz>=16, off<=len and sz<=len-off.
Compile check: font.c, svg.c and ttf.c with the WEBF flags; browser.c (which includes images.c and png.h) with ccheck.sh. No errors. The only warnings are old misleading-indentation and unused-variable ones on lines I did not touch.
Test: /tmp/c1t/p.c (ASan+UBSan, png.h). An IDAT with clen 0x7FFFFFF0, a w of 0x40000001 and a 4-byte IHDR each fail cleanly (rc -8, -2, -2), with no sanitizer errors.
Not done: s4 #5-7 (recursion and cmap budgets), #10 (http), and i4 fuzzer.
