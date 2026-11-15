# cs5 report: GCC -fanalyzer over core/drivers/ui/shell/mem/net/fs

Analyzer setup. The original script was not touched. Copy: /tmp/cs5check.sh. Flags: /tmp/cs5flags. Logs: /tmp/cs5log3/*.log.
- Plain `-fanalyzer` reports **nothing** on these unity files. It silently bails out ("analysis bailed out early", -Wanalyzer-too-complex). Getting real results needs:
  `-fanalyzer -fanalyzer-call-summaries --param analyzer-bb-explosion-factor=200 --param analyzer-max-enodes-per-program-point=8`
- GCC does not treat kmalloc/kfree as an allocator pair, so leak and double-free checks never fire. I fixed that with a forced include, `-include /tmp/cs5attr.h`, which redeclares kmalloc/kcalloc with `__attribute__((malloc, malloc(kfree,1)))`.
- GCC 13.3 has an internal compiler error (ICE) in the analyzer (get_or_create_widening_svalue) on **fs/ntfs/ntfsk.c** and **fs/vfs/unixfs.c**, so those two were NOT analyzed. drivers/usb/xhci.c needs about 3.5 GB of RAM: it runs alone but gets OOM-killed when run in parallel.
- 14 analyzer findings came up. After checking each by hand, 4 are real (below). The rest are false positives where ownership moves into a global table: nvx4.c:128/345 (extents go into g_n4xx), sysroot.c:191-207 (layer blob goes into g_ly), deb_archive.c:387/391 (blob goes to sysroot_add_layer). core/crash.c:482 is the intentional `crash test heap` double free.
- No real findings in core/init/main.c, core/crash.c, drivers/input/input.c, graphics/ui/desktop.c (+ its 4 parts), net/stack/tcpip.c, fs/fat/*, fs/nvx/nvxfs.c, fs/vfs/{vfs,volumes,parts,ramfs,dirtree}.c or fs/pkg/pkg.c. I also read mem/heap.c by hand (kmalloc/kfree/krealloc/coalesce/free lists) and found no bug.

---

## 1. Shell arithmetic `$((...))` applies each operator to the wrong term. MEDIUM, 95%
shell/commands/prompt_and_dispatch.c:99-104 (sh_arith).
The operator that follows term k-1 is stored in `ops[k]` (`terms[nt++]=num; ... ops[nt]=o`), but the loop reads `ops[k-1]`. So term k always gets the operator that came before the previous term, and term 1 always gets the dummy `ops[0]='+'`.
- `echo $((2*3))` gives **5**
- `$((2-3+4))` gives **1** (should be 3)
- `$((10/2))` gives 12

The analyzer flagged line 101: with an empty expression `$(( ))` (nt==0), `v[0]=terms[0]` reads uninitialized stack.
Patch:
```
line 102:  long v[32]; char vo[32]; int nv=0; v[0]=terms[0]; nv=1; vo[0]='+';
     →     if(!nt)return 0; long v[32]; char vo[32]; int nv=0; v[0]=terms[0]; nv=1; vo[0]='+';
line 103:  for(int k=1;k<nt;k++){ char o=ops[k-1]; ...
     →     for(int k=1;k<nt;k++){ char o=ops[k]; ...
```

## 2. Camera stream buffers leak each time a new size is needed. LOW, 85%
drivers/usb/xhci/uvc_webcam.c:25.
`uvc_bufs=uvc_al(bsz*ntd,4096)` overwrites the old buffer whenever `cam.ntd!=ntd` or the buffer must grow. uvc_al returns an *aligned* pointer, so the old block cannot be freed at all. Each time you swap between cameras with different endpoint sizes or bulk/iso modes, up to about 3-4 MB is lost for good. (aud_al at line 215 already uses a capacity variable `e->bcap` and only grows.)
Patch (grow-only, same pattern as audio):
```
add near the other uvc statics:  static u32 uvc_bcap;
line 25:  if(!uvc_bufs||(u32)cam.bufsz<bsz||cam.ntd!=ntd){ uvc_bufs=uvc_al(bsz*(u32)ntd,4096); if(!uvc_bufs){ uvc_m("Out of memory for camera buffers"); return -1; } cam.bufsz=(int)bsz; cam.ntd=ntd; }
     →    if(!uvc_bufs||uvc_bcap<bsz*(u32)ntd){ uvc_bufs=uvc_al(bsz*(u32)ntd,4096); if(!uvc_bufs){ uvc_bcap=0; uvc_m("Out of memory for camera buffers"); return -1; } uvc_bcap=bsz*(u32)ntd; } cam.bufsz=(int)bsz; cam.ntd=ntd;
```
Check before applying: run `grep -n uvc_bufs drivers/usb/xhci/*.c` and confirm that every use indexes with cam.bufsz/cam.ntd and nothing else.

## 3. One partial out-of-memory turns the camera off until reboot. LOW, 90%
drivers/usb/xhci/uvc_webcam.c:28-29.
All three frame buffers are allocated only when `!uvc_fb`. If uvc_fb succeeds and uvc_jpg or uvc_rgb fails (about 9 MB each), uvc_fb stays non-null, the others are never retried, and every later open says "Out of memory for the camera". uvc_fb also leaks.
Patch:
```
line 28:  if(!uvc_fb){ uvc_fb=uvc_al(1920*1200*2+65536,64); uvc_jpg=uvc_al(1920*1200*2+65536+1024,64); uvc_rgb=(u32*)uvc_al(1920*1200*4,64); }
     →    if(!uvc_fb)uvc_fb=uvc_al(1920*1200*2+65536,64); if(!uvc_jpg)uvc_jpg=uvc_al(1920*1200*2+65536+1024,64); if(!uvc_rgb)uvc_rgb=(u32*)uvc_al(1920*1200*4,64);
```

## 4. apt le_push leaks the name string when the target allocation fails. LOW, 95%
fs/pkg/apt/unpack_and_prompts.c:271. `char* a=kmalloc(nl+1); char* b=kmalloc(tl+1); if(!a||!b)return 0;` leaks `a` when `b` fails, and leaks `b` when `a` fails. This only happens under memory pressure during a big install, but that is exactly when apt is short on memory.
```
line 271:  ... if(!a||!b)return 0;
      →    ... if(!a||!b){ kfree(a); kfree(b); return 0; }
```

## 5. Replacing a sysroot layer deletes the old one before the new one is known to be good. LOW, 70%
fs/vfs/sysroot.c:204-207 (sysroot_add_layer). If a layer with the same tag exists, `sysroot_del_layer(tag)` runs first. That frees the old blob and rebuilds the tree. Only then does `sr_addlayer` parse the new blob. If the new blob is corrupt, or memory runs out (sr_parse fails), the function returns 0: the old package's files are gone from / until reboot, and the caller frees the new blob. This is the "error path leaves state half-changed" case.
Today the path is rare: pack_load2 returns early when the pack is already loaded, and apt_boot sees each tag once. It matters for future reinstall or upgrade flows. Small fix: validate first by calling `sr_addlayer` into a temporary tag (e.g. tag+"~new"), then delete the old layer and rename the new one. Alternatively, just document "caller must del first". There is no one-line patch, so it is listed for awareness only.
Related: `sysroot_set_base` returns 1 when a base is already loaded without taking ownership of `blob`, so an owned blob would leak. No current caller passes tag "base" with owned=1.

## Unverified note (not a finding)
mem/heap.c protects itself only with `cpu_irq_save` plus the `g_heap_busy` counter. That is not a lock between CPUs. If any AP (SMP is on at boot) ever calls kmalloc/kfree while CPU 0 does too, the free lists can be corrupted. I did not check whether APs allocate. `g_heap_reent` > 0 in a log would be the sign.
