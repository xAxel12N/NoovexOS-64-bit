# I4 - cheap safety hardening (brainstorm)

## Already exists (do not propose)
- Kernel stack protector: build.sh:4 `-fstack-protector-strong -mstack-protector-guard=global`; guard in core/crash.c:47.
- NX for user pages: EFER.NXE on every CPU, PTE_NX from PROT_EXEC (mem/pmm_vmm.c:171-175, ARCHITECTURE.md:1596).
- CR0.WP set (arch/x86_64/cpu/cpu_init.c:42). Kernel/user separation for Linux and native programs (ring 3, seccomp, mprotect).
- Guard pages around the web engine stack, request stacks and page memory (ARCHITECTURE.md:1201); stk_watch on AP stacks (smp.c:85).
- JS limits: 60 s script limit, string limit, OOM catchable (ARCHITECTURE.md:512,1115). CSP, sandbox, mixed content.
- Fuzzers: tools/tests/linux/lxfuzz.c (syscalls), tools/tests/libc/pfuzz.c (printf), USB keyboard/media/audio fuzzing (6.37), 557 fuzzed HTML pages (13.38).
- JPEG decoder caps W,H<=4096 and W*H<=1920*1200 (lib/image/jpeg.h:12,185).
- tools/webhost.c `nwk_image` is a STUB (returns a fixed 64x48 gradient, webhost.c:175) - so image decoders are NOT fuzzed by webhost.
- Not present: SMEP/SMAP/UMIP (no CR4 bit 20/21/11 anywhere; only listed in cpuinfo.c names), canary randomization, PNG size caps, image-decoder fuzz harness.

## Top 3 new ideas (~10 min each)

### 1. PNG decoder dimension caps (real memory-safety bug, highest value)
lib/image/png.h `png_decode` (line 186+) never bounds IHDR width/height and never checks IHDR length.
- `stride=(w*bpp_bits+7)/8` and `need=h*(1+stride)` are int: w=0x20000000, color 6, depth 16 -> overflow, `need` negative passes `need>rawcap`, then the unfilter loop walks raw with a bogus stride (heap overwrite from any web page image; used by apps/internet/browser/images.c:135,188,197).
- Interlaced path: `o=(fy*w+fx)*4` overflows to negative and passes `o+3>=outcap` -> negative-index write.
- IHDR with clen<13 reads data[4..12] past the chunk (up to 9 bytes past the buffer end).
Fix (in the IHDR branch): `if(clen<13)return -2; if(w<=0||h<=0||w>16384||h>16384)return -11;` and before the interlace branch `if((long)w*h*4>(long)outcap && interlace==1)return -9;`. Optionally also reject invalid depth/color combos per spec. Proof absent: `grep -n "16384\|clen<13" lib/image/png.h` -> nothing; the only checks are need>rawcap / o+3>=outcap which overflow.

### 2. Image decoder fuzz harness (host)
New tools/tests/libc/imgfuzz.c (or tools/tests/web/): `#include "lib/image/png.h"` and `jpeg.h`, xorshift mutate a small valid PNG (all color types/interlace) and JPEG seed (bit flips, IHDR/SOF dims set to huge values, truncation), call png_decode/jpeg_decode with real cap buffers, build with `-fsanitize=address,undefined`, 50k iterations. Register next to pfuzz in tools/check.py:591 (same tuple pattern). Would have caught idea 1. Proof absent: `grep -rln "png.h\|jpeg.h" tools/tests` -> nothing; webhost nwk_image is a stub.

### 3. Random stack-protector canary at boot
core/crash.c:47 `u64 __stack_chk_guard=0x00E4B79A5C3D2F17ULL;` is a fixed public constant (identical on every boot/machine, so a stack overflow can forge it). Fix: in arch/x86_64/boot/boot64.S just before `call kmain` (line 210), seed it: rdtsc (and rdrand if CPUID.1:ECX bit 30), mix, clear the low byte (string terminator), store to `__stack_chk_guard`. Must be done in asm (or a `__attribute__((no_stack_protector))` function called from asm) BEFORE kmain, otherwise kmain's own frame check fails on return. Check the UEFI entry path too (boot64.S:216 second kmain path). Proof absent: `grep -rn "__stack_chk_guard *=" --include=*.c --include=*.S` -> only the constant.

### Runner-up (not 10-min safe): SMEP+UMIP
cpu_init.c could set CR4 bit 20 (SMEP) / 11 (UMIP) when CPUID.7.EBX bit 7 / 7.ECX bit 2. Boot map is supervisor-only (0x83, boot64.S:165) but mem/pmm_vmm.c uses 0x7 for intermediate levels and huge-page splits keep e2&7 (line 237); arch/x86_64/user/ring3.c demo may run with U pages the kernel executes. Needs a check first; make it opt-in (`smep on`) if coded.
