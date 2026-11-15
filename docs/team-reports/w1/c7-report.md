# c7 report: web engine and decoder safety (branch x-c7, 5 commits on top of v11.06 dae38348)

## What changed
1. **lib/image/jpeg.h** (f022c04f). This combines h10 #2-#6, #9, #11 and cs4 #1-#4.
   - :47 jhuff_build clamps nval to 256.
   - :173 resets JPROG at the start of every decode, and adds an nscan counter. Before this, a stale progressive state from the previous image could be reused.
   - :180 rejects L<2.
   - :181 checks that each DQT table fits inside its segment.
   - :183 SOF: checks the segment length (6+3*NC), rejects a second SOF, and rejects NC==2.
   - :184 masks the quant index with &3.
   - :186 adds `W*H*4>rgbamax` to the SOF check, so the progressive path is covered too.
   - :196-197 DHT: checks that the 16 count bytes fit, that tot<=256, and that q+tot<=nx.
   - :200 SOS: requires NC to be set, 1<=ns<=4, and the segment to fit.
   - :201 masks dct/act with &3.
   - :202 progressive scan: returns an error when Se>63 or Ss>Se, and stops decoding after 64 scans.
   - I did not reject Ss==0&&Se!=0, because the DC path ignores Se.
   - Tested in a host harness with ASan and UBSan: baseline, progressive, grayscale-progressive and 4:2:0 images all decode. The pixel checksums are identical to the original decoder. 3000 random mutations each of a baseline and a progressive file gave no memory errors; UBSan reports only signed-overflow arithmetic.
2. **apps/internet/browser/glue/fetch.c:11 and fetch_queue.c:188** (296e9c24). The method is capped at 15 characters, using a bounded scan instead of sl() on a string that could be 200 KB. This covers fetch_queue :269, :273 and :306, because they all print q->meth.
3. **web/css/cssparse.c:827** (06ba3054). Adds `if (depth > 24) return;` at the top of parse_rules, and the 5 nested parse_rules calls (@media, @supports, @container, @layer, @document) now pass depth+1. The import rule `depth<4` and parse_body's own `depth>12` cap are unchanged.
4. **sys/linux/syscalls/timers_and_handles.c:66 and :180** (0b7286b7). apcm_queued returns 0 when boundary==0. REWIND/FORWARD return -EBADFD when boundary or bsize is 0. I checked the other `%boundary` and `%bsize` users: they are all reached only after a state check, or after apcm_pump2's own guard.
5. **sys/linux/lxsock.c:398, netlink only** (0b7286b7). The check is now `if(L<16||L>(u32)(n-off))break;`. No other line in lxsock.c is touched.
6. **#DF redirect** (c1291b92):
   - interrupts.c:60: the web_fault_redirect clause now allows vector 8 when `web_df_ok(r->rsp)` is true. The wk_fault_redirect clause is unchanged and still excludes vector 8.
   - startup.c:10-16: adds a new `estack_guard()` helper, which calls mm_guard_range_bottom and then guards 13 more pages, for a 56 KB guard in total. It also adds `web_df_ok()`, which is true only when g_in_engine is set and rsp is in [lo-1MB, lo+0x10000).
   - The guard is 56 KB rather than 64 KB on purpose: all guard pages stay below lo+0x10000. That keeps them inside the "overflow" branch of web_fault_redirect. With 64 KB, a fault in the top guard page would take the `sp-=2048` branch, which lands in the guard again.
   - The three g_estack allocation sites now call estack_guard: startup.c:15 and :50, and webcache_and_frame_errors.c:69.
   - I checked isr64.S: #DF uses STUB_ERR (it has a hardware error code), and the iretq frame is the same as for #PF/#GP, which are already redirected. IST2 is used only for #DF.
   - Residual risk: an external IRQ that faults while being delivered with rsp already in the guard page loses that IRQ's EOI. The window is tiny, and the old behaviour in that case was a panic anyway.

## Skipped
- h10 #8a-d: the calc(), :is(), GLSL and deep-DOM per-site recursion limits. My task did not list them. The generic #DF redirect now turns these overflows into "page crashed the engine, stopped safely" instead of an RSOD. They are still worth adding later as defence in depth.
- h10 #10 (svg/ttf hangs) and #11 (WebP, chunk parsing, var fan-out): out of scope.

## Tests
- /home/claude/ccheck.sh on browser.c (which includes jpeg.h), webglue.c, interrupts.c, linux.c and lxsock.c gives the same warning count as the baseline.
- cssparse.c was compiled with the build.sh WEBF flags: OK.
- Host fuzz of jpeg.h, as described above.
- QEMU was not run. No USB or input code was touched, so check.py --only usb was not needed.

## Fix round (branch y-c7, on top of v11.07)
- startup.c:16 web_df_ok: the window is now [lo, lo+64K), and it requires !g_web_nest. A forward `extern int g_web_nest;` is added. The 56 KB guard is still fully inside this window.
- interrupts.c:56: new irq_in_service(). It returns true when lapic_isr_top()>=0, or when the ISR of the master or slave PIC is non-zero (read with OCW3 0x0B, the same way as the existing :80 code).
- interrupts.c:61: a #DF is redirected only when `web_df_ok(rsp) && !irq_in_service()`. In every other case it falls through to panic(), the normal red crash screen.
- fetch_queue.c:124-126 nf_init: the guard page is set only after both stk and scr have been allocated, so a guarded page is never kfree'd (r7 LOW 3).
- Compile check: webglue.c gives the same 3 warnings as the baseline, interrupts.c gives 0. Not run in QEMU (r7 LOW 2 is still open: the #DF path has never been executed).
