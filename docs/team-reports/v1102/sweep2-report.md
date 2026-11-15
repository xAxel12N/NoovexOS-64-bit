# sweep2 report -- web/ + apps/internet/browser/ (v11.02 whole-OS sweep)

Context: the web engine runs kernel-side ("kernel side of the web engine", apps/internet/browser/README.md),
so heap corruption here corrupts the kernel heap, and a deep recursion uses the kernel stack. The JS-loop freeze
risk is mitigated: QuickJS has an interrupt callback (web/js/js.c:28), and nwe_check_stop (web/core/engine.c:1961)
lets Esc stop the engine, with a 300 s hard cap.
None of these findings explains the Start-menu freeze in the brief. That path does not go through the browser.

## HIGH
H1 web/text/woff2.c:186-203 + 236-248: WOFF2 table-length sum wraps, causing a huge memcpy into a small buffer (any web font).
  tlen[i] comes from gb128 (up to 2^32-1). `sum += tlen[i]` is unsigned and wraps. The `sum > 64 MiB` check runs after the wrap.
  Example: table0 tlen=0xFFFFFFF0, table1 tlen=0x20 -> sum=0x10, and brotli only has to produce 16 bytes.
  tp[1] then points 4 GiB past `data`. outsz wraps the same way (=60), and memcpy(o+44, np[0], 0xFFFFFFF0) runs.
  Result: kernel heap overflow, then a crash. A remote page triggers it with @font-face.
  Fix: before adding, reject any table where tlen[i] > LIMIT - sum or olen2 > LIMIT. Compute outsz in 64-bit (or with checks).
  Also check that each transformed nl[i] is within its limit.
H2 web/image/imgx.c:216-229 item_data(): AVIF/HEIF extent-length sum wraps, causing a heap overflow.
  tot is summed over up to 8 iloc extents (u64, from the file) before any extent is checked.
  Example: extent0 len=20, extent1 len=2^64-10 -> tot=10 -> malloc(10). Extent 0 passes the per-extent check (20 <= file size).
  memcpy then writes 20 bytes into the 10-byte block. A remote page triggers it with <img src=x.avif>.
  Fix: check every extent (src non-null, off<=lim, len<=lim-off) and accumulate with an overflow check, all before malloc.

## MEDIUM
M1 web/codec/brotli.c:264-270,385: every brotli_decode leaks about 830 KB. It runs with the pool suspended, so nalloc means malloc,
  and store, lh/ih/dh and cmapl/cmapd are never freed. The kernel heap grows by about 1 MB per WOFF2 font decoded (sites load many).
  Fix: nfree all six buffers before pool_resume (or allocate once, statically).
M2 web/image/imgx.c:51,464-467: IX_MAXPX = 16384*16384. A few-hundred-byte AVIF (ispe or grid of 16384x16384)
  makes the kernel malloc 1 GiB and fill it. The same cap applies to JXL (line 491). Result: kernel heap exhaustion and stalls.
  Fix: cap at about 8192x8192 or less, or apply a byte budget shared with the browser image pool.
M3 web/html/dom.c:589-600: document.write recursion has no depth limit. run_script_now -> parse_run(wbuf) -> a <script>
  that document.writes another <script> -> run_script_now ... recurses on the kernel stack, about 1 frame chain per level
  (pstate tb buffer, nm[64], an[96], and the QuickJS eval underneath). I found no depth guard in eng_script_parsed.
  Fix: a static depth counter in run_script_now. Past about 16 levels, append to wbuf and process it iteratively (or drop it).
M4 web/html/dom.c:196-206 (find_id_walk), 256-260 (text_content), 278-300 (serialize), 302-311 (clone_node):
  all recurse once per tree level. The parser caps depth at 511, but JS (appendChild in a loop) can build a tree 100k levels deep.
  Then textContent, innerHTML, cloneNode(true) or getElementById on a long id (>=48 chars, line 244) overflow the kernel stack.
  Fix: walk iteratively (next/parent pointers, as idx_build already does at line 222).
M5 web/text/woff2.c:159,163 rebuild_hmtx indexes xm[i] for i < ngl, where ngl comes from maxp. xm has only ng+1 entries,
  where ng comes from the glyf stream. A mismatched maxp reads heap out of bounds. Fix: pass ng and use xm only when i < ng.

## LOW
L1 web/html/dom.c:555: the foster-parent path writes P->stack[P->sp++] without the <511 guard that push() has. My trace
  peaks at index 511 (still in bounds), but one refactor could turn it into a stack overflow. Use push().
L2 web/html/dom.c:339-353: a numeric entity with up to 10 hex digits goes into a signed int (signed overflow, UB).
  Surrogates D800-DFFF are not mapped to U+FFFD.
L3 web/image/imgx.c:118-124: iloc v2 reads 2 bytes past the box when the box body is exactly 8 bytes.
L4 web/image/imgx.c:487: when a J40 assert fires, the longjmp leaks `copy` and the j40 state every time. The build also
  enables J40 with J40_CONFIRM_THAT_THIS_IS_EXPERIMENTAL_AND_POTENTIALLY_UNSAFE in a kernel-side decoder.
  Consider turning JXL off until it can be fuzzed.

## Checked and found OK
brotli.c bit reader, Huffman tables, block-switch indices, distance/back-reference and dictionary bounds: OK
(only the M1 leak). Brotli meta-block loop always makes progress, so it cannot loop forever.
dom.c open-element stack, attribute parsing, comments, CDATA, raw-text scanning: bounded.
avif grid loops are bounded by ndimg <= 256. The yuv/alpha writers clip to the destination size.

## Coverage (honest; 20-minute budget)
Fully read: web/html/dom.c, web/image/imgx.c, web/codec/brotli.c, web/text/woff2.c,
  web/core/util.c lines 1-60, web/core/engine.c lines 1940-1999.
Skimmed: apps/internet/browser/browser.c, both READMEs.
NOT read: everything else, including css/cssparse.c, cssstyle.c, anim.c, js/js.c, wasm.c, the prelude, layout/*, paint/*
(svg, webgl, canvas), text/ttf.c, font.c, webfont.c, media/wmedia.c, worker/*, core/engine.c (rest), krt.c,
and all of apps/internet/browser (images.c PNG/JPEG decoders, glue/http.c, fetch*.c, websock.c, webwork.c).
Next sweep priority: apps/internet/browser/images.c (PNG/JPEG size math), web/text/ttf.c, glue/http.c
(chunked/length parsing), websock.c framing, paint/svg.c recursion, css nesting recursion.

Not compiled or run; static review only. tools/webhost.c is a host harness that could be used to fuzz these decoders.
