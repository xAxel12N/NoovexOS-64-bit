# h10 report: web/ and apps/internet/browser, the parts not reviewed before

Scope I read: glue/http.c, glue/fetch.c, glue/fetch_queue.c, lib/image/jpeg.h, the WebP path, css/cssparse.c (rule and selector parsing), css/cssstyle.c (calc, var, style_tree), layout.c (box build), paint.c (depth guards), the webgl.c GLSL parser, the js.c fetch, netStart and index natives, and the engine fault and stack setup (startup.c, interrupts.c).
I also re-checked the S4 MEDIUM items. They are still unfixed.
This was a static review only. Nothing was compiled or run.

## Background for several findings: an engine stack overflow kills the whole OS
- The engine runs on a 16 MB kmalloc stack (startup.c:6,50) with a single 4 KB guard page (mem/pmm_vmm.c:246).
- #PF has no IST, so when a deep recursion hits the guard page, the CPU cannot push the #PF frame and raises #DF (IST2).
- interrupts.c:60 deliberately skips `web_fault_redirect` for vector 8, so the result is panic/RSOD, not "the page crashed the engine, stopped safely".
- The `sp < lo+0x10000` branch in web_fault_redirect (startup.c:84) was written for exactly this case, but it is never reached for a real overflow.
- A function whose frame is larger than 4 KB can also step over the single guard page and write into the kernel heap below the stack (see #7).

**Generic fix (recommended, catches every recursion bug below at once).**
File: arch/x86_64/interrupts/interrupts.c:60. In the web_fault_redirect clause only, change
`(r->cs&3)==0&&r->vector!=2&&r->vector!=8&&r->vector!=18&&web_fault_redirect(&r->rip,&r->rsp)`
to
`(r->cs&3)==0&&r->vector!=2&&r->vector!=18&&(r->vector!=8||web_df_ok(r->rsp))&&web_fault_redirect(&r->rip,&r->rsp)`

Add to startup.c:
`int web_df_ok(u64 sp){ u64 lo=(u64)g_estack; return g_in_engine && g_estack && sp < lo + 0x10000 && sp + 0x100000 > lo; }`
plus `extern int web_df_ok(u64);` in interrupts.c.

Also make the guard bigger (64 KB), so frames of 8 KB or more cannot jump over it.
In startup.c:50 and webcache_and_frame_errors.c:69, after `mm_guard_range_bottom(g_estack, ESTACK_SZ);` add:
`for (int gi = 1; gi < 16; gi++) mm_guard((((u64)g_estack + 4095) & ~4095ULL) + 4096 + gi * 4096ULL);`
mm_guard is defined in mm (pmm_vmm.c:228). It needs `extern int mm_guard(u64);`.

Confidence that the panic happens today: 85%. Confidence the redirect works: 75%. The iretq from a #DF frame is architecturally "undefined RIP" but works in practice on QEMU and real hardware, and we replace RIP anyway.

---

## 1. HIGH: a long HTTP method string overflows a kernel stack buffer. Any page with fetch() or XHR triggers it.
Locations:
- glue/fetch.c:11 `q = put(q, method && method[0] ? method : "GET");` writes into `char b[1200]`
- glue/fetch_queue.c:269 and :273 do the same `put(p, q->meth)` into `char b[1200]`
- fetch_queue.c:306 does it in nwk_net_list (only a 400-byte margin).

The method comes straight from JS (js.c:1017 n_fetch, js.c:1113 n_netstart; prelude XHR `this._m = String(m).toUpperCase()` and fetch init.method have no length cap).
nwk_fetch_real keeps only 15 characters for the wire, but the error and log paths use the full string.

Page:
`fetch('http://x.invalid/',{method:'A'.repeat(200000)})`
or a sync XHR with `open('A'.repeat(200000),'http://x.invalid/',false)`.
DNS fails, the code reaches the error path, and 200 KB is copied into a 1200-byte stack buffer.
The engine stack starts 64 bytes below the top of its kmalloc block (startup.c:55), so the copy runs past the end of the 16 MB block into the next kernel heap objects. This is not contained by the fault redirect.
A 4xx reply from a real server triggers it too (the `status >= 400` branch).
Confidence: 90%.

Patch:
- fetch_queue.c:188 change
  `q->url = nq_dup(url, -1); q->meth = nq_dup(method && method[0] ? method : "GET", -1);`
  to
  `q->url = nq_dup(url, -1); { const char* m0 = method && method[0] ? method : "GET"; int ml = sl(m0); q->meth = nq_dup(m0, ml > 15 ? 15 : ml); }`
- fetch.c:11 change
  `q = put(q, method && method[0] ? method : "GET"); q = put(q, " ");`
  to
  `{ const char* m0 = method && method[0] ? method : "GET"; int ml = sl(m0); q = putn(q, m0, ml > 15 ? 15 : ml); } q = put(q, " ");`

## 2. HIGH: JPEG DHT writes up to 4080 bytes into `u8 val[256]` (.bss overflow) — lib/image/jpeg.h:195-196, 47
`tot` is the sum of 16 count bytes, up to 4080. jhuff_build copies `tot` bytes into `h->val[256]`. The copy overwrites the other Huffman tables and the .bss that follows, with attacker bytes, and it also reads past the segment.
This is reached from `<img src=x.jpg>` (images.c:137/190), from opening a JPEG in Files (files_classic.c:249/340), from Writer, and from the camera.
Confidence: 95%.

Patch, jpeg.h:195:
`int tot=0; const u8* bits=q; for(int i=0;i<16;i++)tot+=q[i]; q+=16;`
becomes
`if(q+17>nx)return -4; int tot=0; const u8* bits=q; for(int i=0;i<16;i++)tot+=q[i]; q+=16; if(tot>256||q+tot>nx)return -4;`

## 3. HIGH: JPEG SOS component count overflows `int sc[4]` on the stack; table indexes are not masked — jpeg.h:199-200, 183
- `ns=seg[0]` can be up to 255. `sc[i]=j` writes up to sc[254] when every selector matches a component id. That smashes the kernel stack (return addresses get the values 0..3). Outside the engine (Files, the direct image view through decode_into_pool) this is a straight panic.
- `JC[j].dct=tt>>4` and `act=tt&15` (0..15) index `JHDC[4]`/`JHAC[4]` out of bounds. `JC[i].q=q[2]` (0..255) indexes `JQt[4][64]` out of bounds.

Confidence: 90%.

Patch:
- jpeg.h:199 change `int ns=seg[0];` to `int ns=seg[0]; if(ns<1||ns>4||seg+1+2*ns+3>nx)return -6;`
- :200 change `JC[j].dct=tt>>4; JC[j].act=tt&15;` to `JC[j].dct=(tt>>4)&3; JC[j].act=tt&3;`
- :183 change `JC[i].q=q[2];` to `JC[i].q=q[2]&3;`

## 4. HIGH: progressive JPEG Se > 63 gives an out-of-bounds heap write — jpeg.h:201, 141/148/153/155
`Se=q[1]` can be up to 255. In AC scans, `cf[JZZ[k]]` with k up to Se reads JZZ past its 64 entries, then writes coefficients at a garbage offset from the coefficient buffer.
Confidence: 90%.

Patch, jpeg.h:201: after `int Ss=q[0],Se=q[1],AhAl=q[2],Ah=AhAl>>4,Al=AhAl&15;` add
`if(Se>63||Ss>Se||(Ss==0&&Se!=0))return -7;`

## 5. HIGH: progressive JPEG ignores `rgbamax`, so the output overflows the destination buffer — jpeg.h:185 vs :203
Only the baseline path checks `W*H*4>rgbamax`. A progressive JPEG of 1920x1200 always writes 9.2 MB to `rgba`:
- decode_into_pool passes `cap = IMGPOOL_SZ - imgpool_off`, which can be as small as 8 KB once a few images are loaded. The write then overflows the heap past imgpool.
- files_classic.c passes `viewbuf[6 MB]`, so the write overflows .bss.

Confidence: 90%.

Patch, jpeg.h:185: change `if(W<1||H<1||W>4096||H>4096||W*H>JP_MAXPIX)return -11;`
to `if(W<1||H<1||W>4096||H>4096||W*H>JP_MAXPIX||W*H*4>rgbamax)return -11;`

## 6. MEDIUM: a 2-component JPEG dereferences a NULL or stale `JC[2].plane` — jpeg.h:182, 241
When NC==2, the colour conversion reads `JC[2].plane`. That is NULL on the first decode (panic outside the engine), or a stale pointer with a stale `pw` from the previous image (out-of-bounds read).
Patch, jpeg.h:182: change `if(NC<1||NC>4)return -3;` to `if(NC<1||NC>4||NC==2)return -3;`
Confidence: 85%.

## 7. HIGH: nested `@media{@media{...` recursion has no limit, and each frame is over 8 KB, so it can step over the guard page and corrupt the kernel heap — web/css/cssparse.c:826, 879/882/887/896/905
parse_rules recurses for @media, @supports, @container, @layer and @document and passes the same `depth`, so there is no limit.
Its frame holds `char url[4096]` and `char full[4096]` (:850, :856), so each level is about 8.3 KB, which is larger than the single 4 KB guard page.
About 2000 levels (an 18 KB `<style>`) use up 16 MB. Depending on alignment, rsp either lands in the guard page (#DF, panic) or steps past it and keeps writing return addresses into whatever heap block lies below the engine stack.
Confidence: 85%.

Patch:
- Add `if (depth > 24) return;` as the first line of parse_rules.
- Replace `origin, depth)` with `origin, depth + 1)` in those five nested parse_rules calls (lines 879, 882, 887, 896, 905).
- The `@import ... depth < 4` rule stays correct, because imports are only valid at top level.

## 8. HIGH (with the background note): other unbounded recursions a page can trigger, each leading to #DF and an OS panic
a) **calc() nesting**, cssstyle.c:346/347 → parse_calc → calc_term → calc_atom. About 500 B per level, so about 33K `(` in a 33 KB `width:calc((((((...` causes the overflow.
   Patch: rename `static int parse_calc(` at :478 to `parse_calc_i(`, then add after it:
   `static int g_calc_nest; static int parse_calc(const char** pp, const char* e, lctx* c, L* out) { if (g_calc_nest > 32) return 0; g_calc_nest++; int r = parse_calc_i(pp, e, c, out); g_calc_nest--; return r; }`
   The forward declaration at :342 stays as it is.

b) **Selector nesting `:is(:is(:is(...`**, cssparse.c:210/269 → parse_compound_list → parse_complex. The frame is about 2.5 KB, so about 6500 levels causes the overflow. querySelector from JS takes the same path.
   Patch: rename the definitions at :328/:354 to `parse_has_list_i`/`parse_compound_list_i`, and add wrappers:
   `static int g_sel_nest; static int parse_compound_list(const char* s, const char* e, csel*** out, int forgiving) { if (g_sel_nest > 16) { *out = nalloc(sizeof(csel*)); return forgiving ? 0 : -1; } g_sel_nest++; int r = parse_compound_list_i(s, e, out, forgiving); g_sel_nest--; return r; }`
   Add the same wrapper for parse_has_list (returning 0). Reset `g_sel_nest = 0` in css_reset_all (an nwe_fail longjmp can skip the decrement).

c) **GLSL parser** (WebGL shaderSource), webgl.c:322 p_unary self-recursion (`-----...`), :308 `(` → p_expr, and :459 p_stmt/p_block `{{{{`. None have a depth limit.
   Patch: rename `p_unary` → `p_unary_i` and `p_stmt` → `p_stmt_i`, then add:
   `static int g_gl_nest; static ast* p_unary(gctx* c) { if (g_gl_nest > 200) { cerr(c, PK(c)->line, "expression nested too deeply", 0); c->p = c->nt; return mk(c, A_NUM); } g_gl_nest++; ast* r = p_unary_i(c); g_gl_nest--; return r; }`
   Add the same wrapper for p_stmt (returning `mk(c, A_NUM)`, with a matching forward declaration). Set `g_gl_nest = 0` where the gctx is initialised. Every `(` passes through p_unary, so this one guard covers parentheses, unary chains and call arguments.

d) **Deep DOM built by JS** (`for(i=0;i<300000;i++){d=document.createElement('div');e.appendChild(d);e=d}`) feeds style_tree (cssstyle.c:2333), restyle_children, unstyle_tree and build_elem/build_one (layout.c:122-149). None have a depth limit (layout and paint do have caps of 200/220). The engine pool may run out of memory first; whether the stack or the pool fails first depends on sizeof(style). Confidence that this one triggers the panic: 50%.
   Patch: in js.c's appendChild/insertBefore/replaceChild natives, reject when `depth(parent) > 1024`:
   `{ int d = 0; for (node* a = parent; a; a = a->parent) if (++d > 1024) return JS_ThrowRangeError(ctx, "DOM tree too deep"); }`
   The generic #DF fix covers the rest.

## 9. MEDIUM: progressive JPEG with thousands of SOS markers hangs the kernel — jpeg.h:198-202
Each SOS rescans every block of the image, even after the data runs out: about 2M operations per scan at 1920x1200. A 1 MB file can hold about 100K tiny SOS segments, which is about 2e11 operations. There is no stop check (when not called from the engine, nothing can interrupt it).
Patch:
- Add `int nscan=0;` next to `int Hmax=1,...` at :172.
- At :201 change `if(JPROG){` to `if(JPROG){ if(++nscan>64)break;`
Confidence: 80%.

## 10. MEDIUM: the S4 hangs are still unfixed in v11.06 (listed as "not fixed" in handoff v11.04)
- SVG `<use>` billion laughs (svg.c:525-541), composite-glyph fan-out (ttf.c:217-241), and the cmap scans (ttf.c:100-127). S4's patches (docs/team-reports/v1104/s4-report.md #5-#7) still apply.
- Better fmt12 patch: ADDG drops glyphs ≥ nglyphs without incrementing n, so `n < cap` does not bound the loop. One group of 0..0x10FFFF times about 87K groups in a 1 MB font is about 1e11 iterations.
  ttf.c:124: change `if (b > 0x10FFFF || a > b) continue;` to
  `if (b > 0x10FFFF || a > b || g >= (unsigned)nglyphs) continue; if (b - a > (unsigned)nglyphs - 1 - g) b = a + ((unsigned)nglyphs - 1 - g);`
- fmt4 (:107): a segment loop with `ro` set and all glyph ids 0 never adds anything and runs 64K × 32K segments. Add `int seg_budget = 1 << 20;` before the loop and `if (--seg_budget < 0) break;` inside the inner for.

## 11. LOW
- jpeg.h:180-181 DQT reads 64/128 bytes per table without checking `nx`, giving an out-of-bounds read of up to 129 bytes. Fix: `if(q+1+(pq?128:64)>nx)return -2;` right after `q++;`. Similarly, SOF (:182) reads seg[0..5+3*NC] without a length check: add `if(seg+6+3*seg[5]>nx)return -3;`.
- WebP never decodes. The kernel links third_party/doom/webp_stub.c ("real WebP source was lost"), which always returns -1, while full libwebp 1.4-era sources (with the CVE-2023-4863 fix) sit in third_party/doom/webp/. They are not built, because build_doom.sh only compiles `*.c` in the top directory. The owner will see blank WebP images. This is a feature gap, not a crash.
- http.c:5 and fetch.c:297 chunk-size parsing: the check comes after the multiply, so a size of exactly 0x10000000 followed by one more digit wraps to a small value (the response is cut short or desynced; no memory error). S4 #10 already lists the same class.
- cssstyle.c:694 subst_vars: depth is capped at 8, but the fan-out is not. `--a: var(--b) var(--b) ...` × 1000 per level runs into engine OOM (stops safely) after possibly seconds of work. A total output cap (e.g. abort if `b.n > 1<<20`) would bound it.

## Checked and found OK
- websock framing (S4).
- http.c dechunk, hdr_get, the find_hdr_end and http_recv bounds, and the hc_store/pf_store copies.
- nf_body/nq_grow sizes.
- nq_decode (the int overflow at bodn*8 falls back to 256K).
- js.c jbytes offset/length check, the caret and highlight offsets (clamped), and G_VALUE/selection clamps.
- paint_box/paint_stack/sub_bounds/layout depth caps.
- The libwebp wrapper size check.
