# Part 3: Memory

NoovexOS 64 has no general-purpose physical page allocator covering all of RAM. Memory is a set of fixed physical regions with one allocator each. The low 4 GiB is identity-mapped with 2 MiB pages, so the kernel uses physical addresses as pointers. User processes get their own PML4 with a private window at PML4 slot 8.

| Allocator | Region | Granularity | Freed? | Source |
|---|---|---|---|---|
| `kmalloc` / `kfree` (heap) | largest clean run at least 128 MiB from `0x10000000` | 16 B (+24 B header) | yes (coalescing) | `mem/heap.c` |
| `kslab_alloc` (slab) | 8 KiB slabs carved from the heap | 16 B ... 4 KiB classes | to its free list | `mem/slab.c` |
| `dma_alloc` / `dma_alloc_pages` | DMA arena `0x0B000000-0x0C000000` | 64 B / 4 KiB | never | `mem/pmm_vmm.c` |
| `pframe_alloc` / `pframe_free` | frame pool `0x0C000000-0x0E000000` | 4 KiB | yes (refcounted) | `mem/pmm_vmm.c` |
| fixed `#define` buffers | `0x1090000-0x1240000` | per driver | n/a | drivers |
| `.bss` statics | `0x00C00000-__bss_end` | n/a | n/a | `arch/x86_64/kernel64.ld` |

The memory code is five units: `lib/base/mem.c`, `mem/heap.c`, `mem/pmm_vmm.c`, `mem/slab.c` and `mem/heap_api.c`. Other units reach the allocators through their interface headers (`include/nvx/mem/heap.h`, `include/nvx/mem/pmm_vmm.h` and so on).

---

## 3.1 Boot order

`kmain` (`core/init/main.c`) sets memory up in this order:

```c
{ extern char __bss_end[]; extern char boot_pt_end[];          /* zero .bss but keep the boot page tables */
  for(volatile u64* z=(u64*)boot_pt_end; (u64)z<(u64)__bss_end; z++) *z=0; }
percpu_init_bsp(); inst_snapshot_data();
mm_load();                       /* firmware memory map from 0x7000 (UEFI only) */
...
g_ram_mb=detect_ram_mb();        /* UEFI value from 0x5028, else a 64 MiB-step write probe */
heap_init((int)g_ram_mb);
slab_init();
pframe_init();                   /* frame pool; enabled only if g_ram_mb >= 272 */
if(mm_known()){ ... if(mm_area_conflicts()) klog("memory: WARNING ...run memmap"); }
```

`detect_ram_mb` (`apps/system/files_classic.c`) returns `g_uefi_ram_mb`, capped at 4032, when the UEFI loader supplied one. Otherwise it writes and reads back a pattern at every 64 MiB step from 128 MiB to 4032 MiB and stops at the first step that fails. The result never exceeds 4032 MiB, which matches the 4 GiB identity map.

---

## 3.2 Physical memory map (firmware map)

### 3.2.1 Producer: UEFI loader

`boot/uefi/uefiboot.c` calls `mm_summarize()` after `ExitBootServices` succeeds. The function reduces the EFI map to at most `MM_MAX = 170` regions:

- Type 7 (`EfiConventionalMemory`) stays 7.
- Types 1-4 (loader code/data, boot-services code/data; see `usable_after_exit`) become 4, meaning "usable, but the firmware used it before boot".
- Every other type keeps its EFI number (0 reserved, 9 ACPI tables, 10 ACPI NVS, 11 MMIO, and so on).
- Adjacent regions of the same type are merged. Bit 31 of the count is set when the list overflowed.

The loader copies this summary to physical `0x7000`:

```
0x7000  u32 magic 'NVXM' (0x4D58564E)
0x7004  u32 count | (overflow << 31)
0x7008  entries[count], 24 bytes each: u64 start, u64 end (exclusive), u32 type, u32 pad
```

The loader also works out how much RAM is usable. It starts at `0x10000000`, or at 1 MiB if nothing is usable there, and extends through contiguous usable regions. The end of that run, in MiB, goes to `0x5028` as `g_uefi_ram_mb`. The loader warns when non-usable firmware memory overlaps a kernel area (`{0x5000,0x8000}`, `{0x100000,0x980000}`, `{0x980000,0xC00000}`, `{0xC00000,kbss}`, `{0x10000000,0x11000000}`). It refuses to boot if the loader image or its stack lies where the kernel or the boot information must go.

### 3.2.2 Consumer: `mm_load` and queries (`mem/pmm_vmm.c`)

```c
typedef struct { u64 s,e; u32 t; } mmreg;
#define MMR_MAX 170
static mmreg g_mmr[MMR_MAX]; static int g_mmr_n=0, g_mmr_over=0;
```

`mm_load()` does nothing unless `*(u32*)0x502C == 'UEFI'` (`0x49464555`) and `0x7000` holds `NVXM`. Otherwise it insertion-sorts the entries by start into `g_mmr`, skipping empty ones. After a BIOS or Multiboot boot `g_mmr_n` stays 0, `mm_known()` returns false, and every check below treats memory as acceptable.

| Function | Meaning |
|---|---|
| `mm_ok_type(t)` | `t==7 \|\| t==4` |
| `mm_run_end(c, clean)` | Follows overlapping or adjacent regions from `c` (only type 7 when `clean`, types 7 and 4 otherwise) and returns the end of the contiguous run, or 0 if `c` is not in one |
| `mm_usable_end(c)` | `mm_run_end(c,0)` |
| `mm_clean_region(lo,hi,&bs,&be)` | Largest type-7-only run inside `[lo,hi)`, with its start rounded up to 1 MiB |
| `mm_first_bad_ex(s,e,clean,...)` | First sub-range of `[s,e)` that is not usable (or not clean), with its type; `0xFFFFFFFF` means "not in map" |
| `mm_is_clean(s,e)` / `mm_is_usable(s,e)` | Whole range is type 7 / type 7 or 4 |
| `mm_skip_bad(c,n,top)` | Moves `c` forward, page-aligned, past non-clean regions until `[c,c+n)` is clean. Gives up after 256 steps and returns `top` |
| `mm_tname(t)` | Display name used by `memmap` |

`mm_skip_bad` matters most: the DMA arena uses it so that no buffer lands in memory the firmware has used (section 3.6). `pframe_init` uses `mm_is_clean` per frame for the same reason.

### 3.2.3 Diagnostics (`core/init/installer.c`)

`kareas()` lists the kernel's own areas: boot information, kernel image and stack, USB and disk buffers, page tables and kernel data, DMA arena, page pool, module area, and heap. The DMA arena and page pool are marked `skips`, because they route around bad ranges instead of failing on them. `mm_area_conflicts()` counts non-`skips` areas that overlap non-usable firmware memory; `kmain` logs a warning if any do, and the installer refuses to run. The `memmap` shell command prints the firmware map below 4 GiB, each area with `ok`, `OVERLAPS` or `skips`, the heap range and its sizing mode, `dma_cur`, and the free frame count. `memtest` walks the heap block headers (checking `HBLK_MAGIC`), then fills free DMA-arena space, free frame-pool runs, the module area if unused, and up to 16 x 16 MiB `kmalloc` blocks with an address hash. It reads them back immediately and again after 3 s.

---

## 3.3 Fixed physical regions

All addresses are physical, and also virtual through the identity map.

| Range | Use | Defined in |
|---|---|---|
| `0x5000-0x5040` | Boot information block (64 B, details below) | `boot64.S`, `uefiboot.c`, `core/init/main.c` |
| `0x6000-0x6088` | Multiboot-style info from the UEFI loader; RSDP at `+120`, `'RSDP'` magic at `+128` | `uefiboot.c`, `core/power.c` (`acpi` RSDP lookup) |
| `0x7000-0x7FF8` | Firmware memory-map summary (`NVXM`, 8 + 170x24 B) | `uefiboot.c`, `mm_load` |
| `0x8000-...` | AP start-up trampoline (copied from `ap_tramp_start`) | `arch/x86_64/smp/smp.c`, `ap_trampoline.S` |
| `0x8FD8` / `0x8FE0` / `0x8FF0` / `0x8FF8` | AP parameter block: CPU index, stack top, CR3, `ap_main` | `smp.c` |
| `0xE0000-0x100000` | Read only: scanned in 16-byte steps for the BIOS MP table | `smp.c` line 5 |
| `0x100000` | Kernel load address and `_start` (`. = 0x100000` in `kernel64.ld`); byte 24 holds the size stamp | `kernel64.ld`, `boot64.S` |
| `0x100000-0x1000000` | Kernel image, at most 15 MiB (`KERNEL_MAX` in the UEFI loader, `ASSERT(__file_end <= 0x01000000)` in the linker scripts). It was 8 MiB in v9.08 to v9.10 | `kernel64.ld`, `uefiboot.c` |
| `0x1000000-0x1080000` | Boot stack, `rsp = 0x1080000`, grows down (512 KiB). Before v9.08 the top was `0x700000`, in v9.08 to v9.10 `0x980000` | `boot64.S` |
| `0x1090000-0x10B6000` | xHCI controller 0 memory (`g_xmem`: DCBAA, command ring, ...). Controllers 1-3 use `g_xhc_mem[3][0x26000]` in `.bss` | `drivers/usb/xhci.c` |
| `0x1100000-0x1150000` | USB HID entries: `XHID_BASE(r) = 0x1100000 + r*0x8000`, `XHID_MAX = 10` (input ctx, device ctx, EP0 ring, ...) | `xhci.c` |
| `0x1150000-0x1166000` | USB mass storage: `MSD_INPUTCTX`, `MSD_DEVCTX`, EP0/IN/OUT rings, CBW `0x1155000`, CSW `0x1155100`, 64 KiB data buffer `0x1156000` | `xhci.c` |
| `0x1200000-0x1203000+` | AHCI: command list `AHCI_CLB`, FIS `0x1201000`, command table `0x1202000`, data `AHCI_BUF 0x1203000` | `drivers/storage/gdisk.c` |
| `0x1230000-0x1235000+` | NVMe: ASQ, ACQ, I/O SQ `0x1232000`, I/O CQ `0x1233000`, Identify `0x1234000`, data `NVME_BUF 0x1235000` | `drivers/storage/nvme.c` |
| `0x1300000` | `__bss_start`. First come the boot page tables `pml4`, `pdpt`, `pd0-pd3` and `pdfb` (8 KiB), ending at `boot_pt_end = 0x1308000`. Then all kernel statics, including `SCENE`/`BG` (1920x1080x4 B each), `FS[]`, `pf_bitmap`/`pf_ref`, `g_ap_stacks`, `g_pstack`, and since v9.11 the TLS scratch buffers (`g_tls_inner`, `g_tls_rec`, `g_tls_rb`, `g_tls_mch`) and the HTTPS `g_tls_io`, which used to sit at fixed addresses `0x908000`-`0x930000` | `boot64.S`, `kernel64.ld`, `lib/crypto/nvxtls.c` |
| `__bss_end` (must be at most `0x09800000`) | The linker `ASSERT` fails the build if `.bss` grows past this; in the v9.11 `build/kernel64.elf`, `__bss_end = 0x7D7ADB0` | `kernel64.ld` |
| `0x0B000000-0x0C000000` | DMA arena, 16 MiB (`DMA_ARENA_BASE/TOP`) | `mem/pmm_vmm.c` |
| `0x0C000000-0x0E000000` | Frame pool, 32 MiB = 8192 frames (`PF_BASE/PF_TOP`) | `mem/pmm_vmm.c` |
| `0x0E000000-0x10000000` | Module area. The Multiboot path copies the first module (the Doom WAD) here and records its address and size at `0x5010`/`0x5018` | `boot64.S`, `apps/games/doom.c` |
| `0x10000000-...` | Default heap base; the actual range depends on the firmware map (section 3.5) | `mem/heap.c` |
| Framebuffer (`*(u64*)0x5000`) | Linear framebuffer; mapped explicitly when above 4 GiB (section 3.4) | `boot64.S` |
| LAPIC `0xFEE00000` (default), HPET, ECAM, ... | MMIO below 4 GiB through the identity map; above 4 GiB through `acpi_hi_map` | `smp.c`, `hrtimer.c`, `acpi_dev.c` |

Note: the comment at the top of `mem/pmm_vmm.c` says the frame pool covers "160 MB .. 256 MB". The code defines 192-224 MiB.

Boot information block at `0x5000`, as read by `kmain` and written by `uefiboot.c` and `boot64.S`. The header comment in `boot64.S` describes an older layout and is out of date.

| Offset | Size | Field |
|---|---|---|
| `0x5000` | u64 | framebuffer physical address |
| `0x5008` | u32 | pitch in bytes |
| `0x500C` / `0x500E` | u16 | width / height |
| `0x5010` / `0x5018` | u32 | module (WAD) address / size; cleared unless `0x501C == 'WAD!'` |
| `0x5020` | u8 | bits per pixel |
| `0x5021` | u8 | 1 = RGB framebuffer (UEFI) |
| `0x5028` | u32 | usable RAM in MiB (UEFI) |
| `0x502C` | u32 | `'UEFI'` (`0x49464555`); gates `mm_load`, RAM size and `apic_virtual_wire` |

---

## 3.4 Boot page tables

`boot64.S` reserves its tables at the start of `.bss`. They are 4 KiB-aligned, and `kmain` does not clear them:

```
pml4 (0x1300000)  pdpt (0x1301000)  pd0..pd3 (0x1302000..0x1305FFF)  pdfb (0x1306000, 2 pages)
```

Both entry paths build the same identity map of 0-4 GiB with 2 MiB pages:

```
CR3 -> pml4[0] -(P|RW)-> pdpt[0..3] -(P|RW)-> pd0..pd3, 4 x 512 PDEs
                                               PDE[i] = (i * 2 MiB) | 0x83   (P | RW | PS)
pml4[1..511] = 0            (kernel touches nothing else; slot 8 becomes the user window)
```

- BIOS/stage2 or Multiboot (`start32`): the tables are built in 32-bit code. The code then sets CR4.PAE, loads CR3, sets EFER.LME, sets CR0.PG|PE, far-jumps to `long_entry` with `rsp = 0x1080000`, and calls `kmain`.
- UEFI (`uefi_entry`): the loader has already switched to its own 0-64 GiB identity tables after `ExitBootServices`. The kernel loads its GDT, zeroes the whole `.bss`, and builds the same 4 GiB map. If the GOP framebuffer lies between 4 GiB and 512 GiB, it also fills the two `pdfb` pages and points `pdpt[fb>>30]` and the next entry at them. This maps 2 GiB of framebuffer with `0x83`. It then loads CR3 with `pml4`.

Note: CR3 is never changed for the kernel after boot. `g_kern_cr3` is just the boot `pml4`, saved when the scheduler starts (`core/proc/sched.c`).

### `acpi_hi_map` (`drivers/acpi/acpi_dev.c`)

```c
static u64 acpi_hi_map(u64 pa){
    if(pa<0x100000000ULL)return pa; if(pa>=0x8000000000ULL)return 0;
    ... u64* pdpt=(u64*)(pml4[0]&PTE_ADDR); int i3=(int)(pa>>30);
    if(!(pdpt[i3]&1)){ u64* pd=(u64*)dma_alloc_pages(1); ...
        for(int k=0;k<512;k++)pd[k]=(((u64)i3<<30)+((u64)k<<21))|0x9BULL;
        pdpt[i3]=(u64)pd|3ULL; cpu_set_pt(cr3); }
    return pa; }
```

`acpi_hi_map` identity-maps MMIO between 4 GiB and 512 GiB by adding a whole 1 GiB slot to the shared slot-0 PDPT. The PD comes from the DMA arena. Its pages are 2 MiB with `0x9B` = P|RW|PWT|PCD|PS, which makes them uncached. The function returns `pa` unchanged, because the mapping is an identity map. The HPET (`hrtimer.c`) and the Intel backlight BAR use it. Every process PML4 copies `pml4[0]` (section 3.8), so the new mapping appears in all address spaces at once.

---

## 3.5 Kernel heap (`mem/heap.c`, `mem/heap_api.c`)

### Sizing: `heap_init(ram_mb)`

The rules are applied in order. `g_heap_mapped` records which one won, and `memmap` prints it.

| # | Condition | `heap_base` | `heap_top` | `g_heap_mapped` |
|---|---|---|---|---|
| 0 | default (no firmware map) | `0x10000000` | `ram - 1 MiB` if `ram > base + 128 MiB`, else `base + 128 MiB` | 0 "size from RAM probe" |
| 1 | `mm_clean_region(0x10000000, 0xFC000000)` finds a type-7 run at least 128 MiB | start of the largest run (1 MiB-aligned) | `end - 1 MiB` | 2 "memory the firmware never used" |
| 2 | else `mm_usable_end(0x10000000)` (types 7 and 4, capped at `0xFC000000`) is at least `base + 64 MiB` | `0x10000000` | `ue - 1 MiB` | 1 "limited to firmware RAM" |
| 3 | else scan `c = 0x10000000 ... 0xF0000000` in 1 MiB steps for a usable run at least 128 MiB | `c` | `end - 1 MiB` | 1 |

The initial heap arena always lies below 4 GiB (cap `0xFC000000`), inside the boot identity map. Type-7-only placement (rule 1) means DMA left running by firmware cannot target heap memory. When that arena is exhausted, the heap extends into RAM above 4 GiB on demand (see "High-memory extension" below), so the sub-4 GiB cap bounds only the first arena, not the total heap.

### Allocator

```c
typedef struct hblk { unsigned long size; struct hblk* next; int used; unsigned int magic; } hblk;   /* 24 bytes */
#define HBLK_MAGIC 0x4E56484Bu   /* 'NVHK' */
static hblk* heap_head; static unsigned long long heap_cur, heap_top, heap_base;
```

The heap is a singly linked list of blocks in address order, plus a bump pointer `heap_cur`:

```
heap_base                                   heap_cur                 heap_top
|[hdr|payload][hdr|payload (free)][hdr|payload]|<----- never used ----->|
 +- next ------>+- next ---------->+- next=0
```

- `kmalloc(n)`: rounds `n` up to 16. It first searches the list first-fit for a free block with `size >= n`. If the leftover is at least `n + 24 + 32`, the block is split and the tail becomes a new free block. If no block fits, it bumps `heap_cur` by `24 + n` and walks the list to append the new block, so this path is O(blocks). It returns 0 when `heap_cur + need > heap_top`. Payloads start 24 bytes after a 16-aligned header, so they are only 8-byte aligned.
- `kfree(p)`: checks the header with `hblk_ok`: a valid arena address (the low arena or any high-memory arena), 8-aligned, magic matches. A bad pointer is ignored without any message. It marks the block free, then walks the whole list and merges each free block with physically adjacent free successors. `heap_cur` never moves back, so freed memory is reused only through the list.
- `krealloc(p,n)`: returns `p` if the old size is already at least `n` (it never shrinks). Otherwise it does `kmalloc`, copies, and `kfree`s the old block.
- `kcalloc(a,b)`: `kmalloc(a*b)` plus zeroing. It does not check `a*b` for overflow.
- Every list walk is bounded at 1,000,000 iterations so that a damaged list cannot hang the kernel.

`mem/heap_api.c` exports `malloc`, `free`, `realloc` and `calloc` as non-static aliases, so that ported C libraries (Lua, ntfs-3g, zstd, ...) link unchanged. It also defines `heap_used()`, the sum of used block sizes; unlike the allocator, it walks without `hblk_ok` and without a bound. The `mem` command prints the heap base, total size, bytes in use, bytes reserved (`heap_cur - heap_base`), and the result of a 1 MiB allocate/write/free self-test. Large consumers allocate on first use; for example `apps/games/doom.c` takes `kmalloc(24 MiB)` for Doom's private heap.

### High-memory extension

The low arena is bounded below 4 GiB, but a laptop's usable RAM mostly sits above 4 GiB, and the sub-4 GiB window is further eaten by the PCI hole, the kernel image and the sysroot overlay. A large `sudo apt install` needs transient RAM per package (the downloaded `.deb`, the decompressed `data.tar`, the package index), and on the live ISO the writable filesystem is RAM-backed as well. Confined to the low arena, these exhausted it and failed with `E: out of space or memory while unpacking` even though gigabytes above 4 GiB were free.

`kmalloc` therefore keeps a small table of extra arenas. When neither the free list nor the low arena can satisfy a request, it rounds the request up (at least a 64 MiB chunk, 2 MiB-aligned) and calls `hp_alloc_run(pages)` in `mem/pmm_vmm.c`, which scans the high frame pool for a contiguous run of free frames within one region, marks them used, zeroes them, and returns the base. Because `mm_map_high` identity-maps every high RAM region with 2 MiB pages at boot, that physical base is directly usable as a kernel pointer. The run becomes a new bump arena; later allocations fill the low arena and every high arena that has room. `hblk_ok` (used by `kfree`, `krealloc` and the list walks) accepts a pointer in the low arena or in any high arena, so frees and coalescing work uniformly across them. Coalescing never merges across arenas, since the physical-adjacency test in `kfree` only joins neighbours within one contiguous run.

The extension depends on the high frame pool being up (`pf_ready`, RAM at least 500 MiB), which `hp_init` validates at boot with a three-point write/read probe before the pool is used. Drivers that need a buffer below 4 GiB for 32-bit DMA use `dma_alloc` (section 3.6), not `kmalloc`, and their init-time allocations run while the low arena is still plentiful, so they are unaffected.

### Slab allocator (`mem/slab.c`)

```c
#define SLAB_BACKING 8192ULL
static const unsigned long SLAB_SIZES[9]={16,32,64,128,256,512,1024,2048,4096};
```

Each `slab_cache` keeps a LIFO free list of objects. `slab_grow` takes one 8 KiB block from `kmalloc`, links it into `pages` for statistics, and cuts the rest (`(8192 - 8) / objsize` objects) into the free list.

| Call | Behaviour |
|---|---|
| `kslab_alloc(n)` | Smallest class at least `n`; pops the free list in O(1); `kmalloc(n)` if `n > 4096` or the slab cannot grow |
| `kslab_free_sz(p,n)` | Caller passes the size; pushes onto that class's list, or `kfree` if `n > 4096` |
| `kslab_talloc` / `kslab_tfree` | Stores `n+8` in an 8-byte prefix so the free does not need the size |
| `slab_stats` | Backs the `slabinfo` command: size, live objects, slabs per class |

Backing slabs are never given back to the heap. The cache has no lock; it relies on the big kernel lock that `isr_dispatch` takes (`bkl_lock`). Nothing else in the tree calls `kslab_*` apart from `slabinfo`.

---

## 3.6 DMA arena (`dma_alloc`, `dma_alloc_pages`)

```c
static unsigned long long dma_cur=DMA_ARENA_BASE;   /* 0x0B000000 */
static void* dma_alloc(unsigned long n){ n=(n+63)&~63UL; u64 c=mm_skip_bad(dma_cur,n,DMA_ARENA_TOP);
    if(c+n>DMA_ARENA_TOP)return 0; dma_cur=c; void* p=(void*)dma_cur; dma_cur+=n; /* zero */ return p; }
static void* dma_alloc_pages(unsigned long pages){ /* align dma_cur to 4 KiB, skip bad, bump, zero */ }
```

The arena is a bump allocator over 16 MiB of identity-mapped memory below 4 GiB, with no free. Allocations are zeroed and 64-byte aligned, or page-aligned for `dma_alloc_pages`. `mm_skip_bad(..., clean)` steps over any range the firmware map does not list as type 7. Callers allocate once and cache the pointer in a `static`.

| Caller | Purpose |
|---|---|
| `drivers/net/wifi/rtw8821ce.c`, `net/wifi/*.c` | Wi-Fi TX/beacon packet buffers |
| `shell/commands.c` (`RXNUM`/`BENUM` rings) | Wi-Fi RX and beacon rings |
| `drivers/usb/xhci.c` | xHCI scratchpad buffers when a controller needs more than 32 |
| `drivers/video/nvidia/nvdisp.c` | display push buffer |
| `drivers/acpi/acpi_dev.c` | `acpi_hi_map` page directories |
| `core/proc/elf.c` | `nvx_dlload` module images and `nvx_alloc` for modules |
| `sys/native/kapi.c` | `kapi_dma_pages` for loadable driver modules |

---

## 3.7 Frame pool (`pframe_*`)

```c
#define PF_BASE 0x0C000000ULL
#define PF_TOP  0x0E000000ULL
#define PF_N    ((u32)((PF_TOP-PF_BASE)/4096ULL))     /* 8192: the fixed 32 MiB part */
#define PF_MAXN (PF_N+524288u)                        /* plus at most 2 GiB taken from the heap */
u8  pf_bitmap[(PF_MAXN+7)/8];  static u16 pf_ref[PF_MAXN];
u32 pf_n;  u64 pf_x_base, pf_x_top;                    /* frames in use by the pool, extension range */
```

The pool has two parts. Frame numbers below `PF_N` are the fixed 32 MiB at `PF_BASE`. Frame numbers from `PF_N` up to `pf_n` are an extension range `[pf_x_base, pf_x_top)` carved out of the top of the kernel heap at boot, so the pool grows with installed RAM. `pf_idx(pa)` maps a physical address to a frame number (or -1), `pf_pa(i)` maps back, and `pf_owned(pa)` tells whether an address belongs to the pool.

- `pframe_init` sets `pf_ready = (g_ram_mb >= 272)` and then calls `pframe_grow_from_heap`. If the heap has at least 256 MiB free, half of the free heap (rounded down to 2 MiB, at most 2 GiB) is taken from its top: `heap_top` is lowered, and at least 128 MiB of heap stays above `heap_cur`. With 1 GiB of RAM the pool is about 330 MiB; with 8 GiB it is the full 2 GiB plus 32 MiB.
- The bitmap is then cleared for `pf_n` frames. Every frame that is not clean (`!mm_is_clean`) is marked used up front, so firmware-used frames are never handed out. With less than 272 MiB the pool stays off, and ring-3 programs cannot run; `kmain` logs this.
- `pframe_alloc` scans the bitmap first-fit starting at a moving hint (`pf_hint`), sets `pf_ref = 1`, zeroes the 4 KiB, and returns the physical address, or 0 if none is free.
- `pframe_free(pa)` ignores addresses outside the pool and frames not marked used. If `pf_ref > 1` it only decrements; otherwise it clears the bit.
- `pframe_ref(pa)` adds a reference to an allocated frame, saturating at `0xFFFF`.
- `pframe_refs(pa)` returns the reference count. It returns 1 for any address outside the pool, so the copy-on-write code treats those pages as unshared.
- `pframe_free_count()` returns `pf_n - pf_used`, or 0 when the pool is off. It is used for the ENOMEM checks in `mmap` and `brk`, and as `freeram` in Linux `sysinfo` (`sys/linux/linux.c`).

The pool supplies every user page, every user page-table level (PML4/PDPT/PD/PT), and shared-memory segments (`shm_get_k` in `core/proc/sched.c`). Code that walks frames by number (the installer's memory test, the `mem` command) uses `pf_n` and `pf_pa`; code that asks "is this a pool frame" (copy-on-write in `vm_clone_user`) uses `pf_owned`.

Before v8.97 the pool was only the fixed 32 MiB, whatever the RAM size, so a single large `malloc` in a musl program failed with ENOMEM. `featest` now allocates and touches 96 MiB with `malloc` and two 64 MiB `mmap`s in one process.

---

## 3.8 Virtual memory (`mem/pmm_vmm.c`, `core/proc/jobs.c`)

### PTE format

| Bit(s) | Mask | Meaning in NoovexOS |
|---|---|---|
| 0 | `0x001` | Present |
| 1 | `0x002` | Writable |
| 2 | `0x004` | User |
| 7 | `0x080` | PS (2 MiB/1 GiB page) in PDE/PDPTE; `vm_walk` refuses to descend through one |
| 9 | `0x200` | `PTE_COW`: software bit, present but read-only, copy on write |
| 10 | `0x400` | `PTE_LAZY`: software bit, not present, allocate a zeroed frame on first touch |
| 12-51 | `PTE_ADDR = 0x000FFFFFFFFFF000` | frame address |

User mappings use flags `0x7` (P|RW|US) whatever the ELF segment or `mmap` `prot` says. NX is not used. Intermediate tables are always created with `0x7`.

### Address-space primitives

```c
static u64 vm_new_pml4(void){ u64 p=pframe_alloc(); ... u64* k=(u64*)(cpu_get_pt()&~0xFFFULL);
    for(int i=0;i<512;i++)t[i]=k[i];   /* share the kernel mappings */
    t[8]=0; return p; }
```

`vm_new_pml4` copies all 512 entries of the current PML4. Only slot 0, the identity map, is populated in the kernel's PML4, so the kernel's PDPT and PDs are shared by reference. Slot 8 is then cleared for the new space.

`vm_walk(pml4, v, create)` returns a pointer to the PT entry for `v`:

```
 v (48-bit):  | 47..39 | 38..30 | 29..21 | 20..12 | 11..0 |
                 i4       i3       i2       i1     offset
 reject v < 0x8000000000 (slot 0 = kernel) or v >= 0x800000000000
 CR3/pml4pa -> PML4[i4] -> PDPT[i3] -> PD[i2] -> PT[i1]  <-- returned pointer
                 | !P && create: pframe_alloc() | 0x7 at each level
                 | !P && !create: return 0
                 + PDPT/PD entry with PS (0x80): return 0
```

Table frames are reached through their physical addresses, which works because the pool lies inside the identity map.

| Function | Behaviour |
|---|---|
| `vm_map(pml4,v,pa,flags)` | Walk with create, `*e = pa \| flags \| P`, `invlpg v` |
| `vm_unmap(pml4,v)` | `*e = 0`, `invlpg`; does not free the frame |
| `vm_map_lazy(pml4,v)` | Creates tables; if the PTE is not present, sets it to `PTE_LAZY` |
| `vm_pte_used(e)` | `(e & P) \|\| (e & PTE_LAZY)` |
| `vm_selftest` | Maps the same VA (`0x8000000000`, slot 1) to different frames in two spaces and checks isolation |
| `vm_free_user(cr3)` (`jobs.c`) | Walks slot 8: `pframe_free` on every present leaf (refcount-aware), then each PT, PD and the PDPT; clears `pml4[8]`; frees the PML4 frame. Lazy entries need no freeing |
| `vm_clone_user(src)` (`jobs.c`) | Copy-on-write fork, below |

`invlpg` flushes only the current CPU's TLB. Nothing performs a TLB shootdown.

### `vm_clone_user`: copy-on-write fork

For every used PTE in the parent's slot 8:

1. Lazy: the child gets `PTE_LAZY`, and both sides fault in their own zeroed page later.
2. Frame outside the pool (`!pf_owned(pa)`): copied eagerly into a new frame.
3. Writable pool frame: the parent PTE becomes `(e & ~RW) | PTE_COW`, and the child gets the same value.
4. Read-only pool frame: shared as it is.

Cases 3 and 4 then call `pframe_ref(pa)`. If any parent PTE changed and the parent is the current space, CR3 is reloaded to flush the TLB, and `smp_tlb_sync(src)` makes every other core that is running a thread of the parent flush too, so none of them can keep writing to a page that is now shared with the child. `jc_fork` calls this. The older demo scheduler's `do_fork` in `core/proc/sched.c` still copies every page eagerly.

### CR0.WP

`arch_fpu_init` (`arch/x86_64/cpu/cpu_init.c`), which runs on the BSP from `kmain` and on each AP from `smp_sched.c`, sets CR0 bit 16 (WP). Ring-0 writes to read-only user pages therefore fault too. Before the kernel writes through a user pointer it calls `jc_umapped(p,n)` (`jobs.c`). This checks that the range lies in the user window, faults in lazy and COW pages with `vm_fault_user(cr3, v, 1)`, and requires `(e & 5) == 5` (present and user).

---

## 3.9 User address space

User space is the single PML4 slot 8, `0x400_0000_0000-0x7FF_FFFF_FFFF` (4 TiB-8 TiB). `jc_uok(p,n)` accepts exactly `0x40000000000 <= p` and `p+n < 0x80000000000`, with no wraparound.

```
0x800_0000_0000 +- end of window (jc_uok upper bound)
                |   ... mmap grows upward (g_mmap_next is a high-water mark)
0x400_0400_0000 +- mmap base       (p->mmnext / g_mmap_next initial value)
                |   brk heap, 32 MiB max (lx_brk accepts [0x40002000000, 0x40004000000))
0x400_0200_0000 +- brk start       (p->brk / g_ubrk) = ELF_IMG_TOP
                |   ... ELF segments must fit in [ELF_IMG_LO, ELF_IMG_TOP)
0x400_0060_0000 +- SHM_VADDR: shm segment id at +id*1 MiB (legacy shm_map_k)
0x400_0040_0000 +- ELF_PIE_BASE: ET_DYN (static-PIE) load bias
0x400_0020_0000 +- legacy thread stacks (g_thread_stk_next, +64 KiB each)
0x7F0_0000_0000 +- ELF_INTERP_BASE: the dynamic linker (/lib/ld-musl-x86_64.so.1) of a dynamic program
                |   ... mmap area continues below
0x400_0010_1000 +- initial stack top (USTK_V + 0x1000)
0x400_0010_0000 +- USTK_V: stack page 0 (eager)
0x400_000F_F000 +- stack page 1 (eager)
0x400_000F_1000 +- ELF_STK_LO: pages 2..15 are PTE_LAZY; the image may not reach above this
                |   pages 16..255 are also PTE_LAZY when free (JS_STK_PAGES = 256, up to 1 MiB),
                |   stopping one guard page above anything already mapped (a native program's image)
0x400_0005_0000 +- legacy demo "id byte" page (sched.c)
0x400_0000_2000 +- ring3.c self-test stack
0x400_0000_0000 +- ELF_IMG_LO / ring3.c self-test code page
```

### ELF loading (`elf_build_space`, `core/proc/elf.c`)

- Accepts ELF64 x86-64 `ET_EXEC` (bias 0) or `ET_DYN` (bias `ELF_PIE_BASE`). The program headers must lie inside the file and number from 1 to 64.
- A `PT_INTERP` header (at most 127 bytes) makes it a dynamic program; only `ET_DYN` (PIE) programs may have one. After the program's own segments are mapped, `elf_load_interp` looks the interpreter up with `jc_find_lib` (section 6.10), checks that it is an x86-64 `ET_DYN` file, and maps its `PT_LOAD` segments at `ELF_INTERP_BASE`. The entry point becomes the interpreter's entry, `AT_BASE` is set to `ELF_INTERP_BASE`, and `AT_ENTRY` and `AT_PHDR` still describe the program, which is what musl's dynamic linker expects.
- Each `PT_LOAD` must fit in `[ELF_IMG_LO, ELF_IMG_TOP)`, must not overlap the stack range `[ELF_STK_LO, USTK_V+0x1000)`, and must have `filesz <= memsz`. The entry point must fall inside an executable segment.
- Pages are allocated eagerly with `pframe_alloc` and mapped `0x7`. Bytes are copied up to `filesz` and zeroed up to `memsz`. A page shared by two segments is reused.
- Two stack frames are mapped eagerly at `USTK_V` and `USTK_V - 0x1000`. An 8 KiB image built in `g_stkimg` is copied into them: argc, argv, envp, the auxv entries (`AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, `AT_PAGESZ`, `AT_ENTRY`, uid/gid, `AT_HWCAP`, `AT_CLKTCK = 100`, `AT_RANDOM` with 16 xorshift-of-TSC bytes, `AT_EXECFN`), and the strings.
- Any failure calls `vm_free_user(sp)` on the half-built space.

`jc_build` (`jobs.c`) then marks stack pages 1 to 255 below `USTK_V` lazy where they are still unused. From page 16 on it stops as soon as the next page down is in use, so a native program linked at `0x40000000000` keeps a guard page between its image and its stack. `jc_create` sets `p->brk = 0x40002000000` and `p->mmnext = 0x40004000000`. `jc_exec` builds the new space first, switches to `g_kern_cr3`, frees the old space, and resets `g_ubrk` and `g_mmap_next`. While a process runs, its `brk` and `mmnext` live in the per-CPU variables `g_ubrk` and `g_mmap_next`. They belong to the process, not the thread, so they are kept in the thread-group leader's `struct jproc` (section 5.15): `jc_enter` loads them from the leader, `linux_syscall` reloads them from the leader before every Linux system call and stores them back after it, and for native programs the end of `jc_enter` stores them back.

---

## 3.10 `mmap`, `munmap`, `brk` (`sys/linux/linux.c`)

| Linux nr | Handler | Behaviour |
|---|---|---|
| 9 | `lx_mmap` | See below |
| 10 `mprotect` | `return 0` | No-op; every page stays RW |
| 11 | `lx_munmap` | Page-aligned `a`, non-zero `len`, inside the window, not overlapping the stack range (EINVAL otherwise); then `lx_unmap_range` |
| 12 | `lx_brk` | Lazy growth and shrink, below |
| 25 `mremap` | `-ENOMEM` | Unsupported |
| 28 `madvise` | `return 0` | No-op |

`lx_mmap(addr,len,prot,flags,fd,off)`: `prot` is ignored.

1. Returns EINVAL if `len` is 0 or `off` is not page-aligned, and ENOMEM if `len > 1 GiB`.
2. Without `MAP_ANONYMOUS` (0x20), `fd` must refer to a RAM-file (`v->dev == 0 && fs_idx >= 0`); otherwise ENODEV.
3. Returns ENOMEM if `pages > pframe_free_count()`, even for lazy anonymous mappings.
4. With `MAP_FIXED` (0x10), `addr` must be page-aligned, inside the window and clear of the stack range. Whatever is mapped there first is removed with `lx_unmap_range`.
5. Otherwise `lx_find_gap(cr3, pages+1)`, which leaves one guard page, scans from `0x40004000000` up to `g_mmap_next`. A missing page table counts as free and skips to the next 2 MiB boundary; a used PTE resets the run. If no gap is found, the mapping goes at `g_mmap_next`, which is then raised past the new mapping.
6. Anonymous pages become `PTE_LAZY`. File pages get frames allocated eagerly and filled from `fsd(k)`, zero-padded past EOF. These are always private copies: `MAP_SHARED` writes never reach the file. If any step fails, the pages already mapped are released again.

`lx_unmap_range(a,e)` visits each page. A present page has its PTE cleared and `invlpg` run, and its frame is freed. A lazy page just has its PTE cleared. Page tables are not freed until `vm_free_user`. If other threads of the process exist, the frames are first collected (64 at a time) and `smp_tlb_sync(cr3)` runs before they are freed, so no other core can still be writing to a frame through a stale TLB entry (section 4.15).

`lx_brk(want)` returns the current break unless `want` lies in `[0x40002000000, 0x40004000000)`. To grow, it rounds up to a page, checks the free frame count, and refuses if any target page is already used, for example by a `MAP_FIXED` mapping. It then `vm_map_lazy`s each page and advances `g_ubrk`. To shrink, it calls `lx_unmap_range(top, g_ubrk)`. The exact requested value is kept in `p->lxbrk`. The native NoovexOS syscall 12 (`jobs.c`) instead maps frames eagerly, page by page, until it reaches the target or runs out of frames.

---

## 3.11 Page-fault path and OOM

```
#PF (vector 14) -> isr64.S -> isr_dispatch (bkl_lock) -> isr_body(r)
   +- vm_user_pf(r)                               core/proc/jobs.c
        +- !g_js_running || g_jcur<0     -> 0 (not handled)
        +- va = CR2; !jc_uok(va,1)        -> 0
        +- rc = vm_fault_user(CR3, va, errcode bit 1 = write)
             +- rc == 0  -> return 1: retry the instruction
             +- rc == -2 -> jc_finish(g_jcur, 9); jc_leave()   (killed: out of frames)
             +- rc == -1 -> 0
   not handled: vector < 32 && ring 3 -> jc_fault: SIGSEGV(11), or a pending signal if a handler is installed
                vector < 32 && ring 0 -> panic(r) (red screen)
```

```c
static int vm_fault_user(u64 cr3,u64 va,int write){
    u64* e=vm_walk(cr3&~0xFFFULL,va&~0xFFFULL,0); if(!e)return -1;
    if(!(*e&1)){ if(!(*e&PTE_LAZY))return -1;                       /* demand-zero */
        u64 f=pframe_alloc(); if(!f)return -2; *e=f|0x7ULL; invlpg; return 0; }
    if(!(*e&4))return -1;
    if(write&&!(*e&2)){ if(!(*e&PTE_COW))return -1;
        u64 pa=*e&PTE_ADDR;
        if(pframe_refs(pa)<=1){ *e=pa|0x7ULL; invlpg; return 0; }        /* last owner: just unprotect */
        u64 f=pframe_alloc(); if(!f)return -2; copy 4 KiB pa->f;
        *e=f|0x7ULL; invlpg; smp_tlb_sync(cr3); pframe_free(pa); return 0; } /* other threads must drop the old page first */
    return 0; }
```

The handler does not look at the faulting privilege level, so a kernel access through a user pointer with CR0.WP set is resolved in the same way. Faults outside the window or on unmapped kernel addresses still reach `panic`. OOM shows up in two forms:

- At fault time: the pool is empty, the process is finished with status 9 (SIGKILL), and the scheduler moves on.
- At syscall time: `mmap`, `brk` and `elf_build_space` return `-ENOMEM`, the old break, or failure.

No reclaim path frees frames for user memory. `fs_evict` (section 3.12) gives back heap memory only.

---

## 3.12 File-system memory (`fs/vfs/ramfs.c`)

Every file lives in `FS[FS_N = 1024]` (`FsFile`). Its contents are a single contiguous `kmalloc` buffer, `data`, holding `len` bytes plus a NUL, with `cap` the allocated size. Disk backends (NVX4 or the root file system) load contents on demand.

| Function | Rule |
|---|---|
| `fs_reserve(k, need)` | Returns at once if `cap > need`. Otherwise the new capacity is `cap * 1.5`, or 64 for an empty file, at least `need+1`, at most `FS_CAP+1` (1 GiB), rounded up to 64. If that allocation fails it retries with exactly `need+64`. It copies `min(len, nc-1)` bytes and frees the old buffer |
| `fs_alloc(k, n)` | `kmalloc`; on failure `fs_evict(n+4096, k)` and one retry |
| `fs_evict(want, keep)` | Two passes over all files. It skips `keep`, files with no data, lazy or dirty (`!clean`) files, files touched within the last 64 (pass 0) or 8 (pass 1) `fs_ensure` ticks, open files, and files that `fs_reloadable` (`fs/nvx/nvx4.c`) rejects: orphans, no disk, files in a tmp directory, or NVX4 files not fully stored. Evicted files have their buffer freed and `lazy = 1` |
| `fs_ensure(k)` | Bumps `tick` (LRU). If lazy, it reloads with `fs_backend_load` (`nk_root_read` or `n4_read_file`) and marks the file clean. If there is no data, it allocates a zero-filled buffer |
| `fsd(k)` | Returns `fs_ensure(k)` data, or the static 64 KiB `g_fs_zero` on failure |
| `fs_set` | Frees an oversized buffer (`cap > n + cap/2 + 64 KiB`) before writing |
| `fs_truncate_to(k, 0)` | Frees buffers larger than 64 KiB |
| `fs_dirty` / `fs_wb_mark` | Track the dirty byte range `[dlo, dhi)`; write-back is scheduled about 100 ticks later |

The file cache and every other `kmalloc` user share one heap. When an allocation through `fs_alloc` fails, clean, idle, reloadable files are dropped and re-read later. `lx_mmap` of a file goes through `fsd()` and copies the contents into frame-pool pages, so mapped files never pin heap buffers.

---

## 3.13 Summary of limits

| Resource | Limit | Where |
|---|---|---|
| Physical RAM recognised | all RAM in the firmware map; memory above 4 GiB is mapped and pooled up to 512 GiB (tested to 128 GiB) | `detect_ram_mb`, `mm_map_high`, `hp_init` |
| Firmware map entries | 170 (overflow flagged) | `MMR_MAX`, `MM_MAX` |
| Kernel image | 100 MiB (since v9.22; section 3.15) | `KERNEL_MAX`, linker `ASSERT`, `build.sh` |
| `.bss` | `0x10000000` - `0x18000000` (since v9.22) | `kernel64.ld` |
| DMA arena | 16 MiB, never freed | `DMA_ARENA_*` |
| Frame pool (all user memory and page tables) | 32 MiB plus half the free heap, at most 2 GiB more, plus all clean RAM above 4 GiB (up to 32 regions); needs at least 272 MiB RAM | `PF_*`, `pframe_grow_from_heap`, `hp_init` |
| Heap | at least 64-128 MiB; ends at most `0xFC000000 - 1 MiB` | `heap_init` |
| User ELF image | 32 MiB window | `ELF_IMG_LO/TOP` |
| User stack | up to 256 pages, 1 MiB (2 eager, the rest lazy) | `JS_STK_PAGES` |
| `brk` heap | 32 MiB | `lx_brk` |
| Single `mmap` | 1 GiB, and no more pages than free frames | `lx_mmap` |
| RAM file size | 1 GiB (`FS_CAP`), 1024 files | `fs/vfs/ramfs.c` |

## 3.14 Memory above 4 GiB

Before v9.14 the kernel identity-mapped only the first 4 GiB (`boot64.S`) and capped the RAM size at 4032 MiB. Three pieces now make the rest usable.

### The memory map on BIOS boots

`mm_load` used to accept only the UEFI summary at `0x7000` (magic `NVXM`). BIOS boots now leave a second format there, magic `0x4558564E` ("NVXE"): a count at `0x7004` and up to 40 raw E820 entries (base, length, type, attributes; 24 bytes each) from `0x7008`.

| Boot path | Who writes it |
|---|---|
| myboot (the ISO and USB image) | `boot64.S`, Multiboot branch: copies `mmap_addr`/`mmap_length` from the Multiboot info when flag bit 6 is set |
| Native CD loader | `cdboot.S`, `e820_scan` (INT 15h, EAX=E820) |
| Native disk loader | `stage2.S`, `e820_scan` |

`mm_load` converts the E820 types to the UEFI numbering the rest of the code uses: 1 becomes 7 (usable), 3 becomes 9 (ACPI tables), 4 becomes 10 (ACPI NVS), 5 becomes 8 (unusable), everything else 0 (reserved).

`detect_ram_mb` now returns the sum of all usable regions (types 7 and 4) when a map is known, so `g_ram_mb`, `/proc/meminfo` and Settings show the real size. `heap_init` clamps the heap top to `0xFC000000` so a large RAM size never pushes the heap above 4 GiB.

### Mapping: `mm_map_high` (`mem/pmm_vmm.c`)

For every 1 GiB slot from 4 GiB to 512 GiB whose PDPT entry is empty and that overlaps usable RAM, a page directory is taken from the heap (one 4 KiB page per slot, 64 KiB for 16 GiB, 512 KiB for 128 GiB). Each 2 MiB entry that overlaps usable RAM is mapped (`0x83`: present, writable, large page). Slots already in use (the framebuffer window, `acpi_hi_map`) are left alone. MMIO between RAM regions is not mapped.

### The high frame pool (`hp_*`)

`hp_init` runs at the end of `pframe_init`. It collects the 2 MiB chunks above 4 GiB that are clean (type 7) and mapped into at most 32 ranges, then carves its bookkeeping from the start of the largest range: a bitmap (one bit per frame, in 64-bit words) and a 16-bit reference count per frame, about 2.1 bytes per 4 KiB frame (68 MiB for 128 GiB). Three frames (first, middle, last) are written and read back; if any check fails the pool is disabled.

| Function | Behaviour |
|---|---|
| `pframe_alloc` | high pool first (skips full 64-bit words with one compare), then the old pool below 4 GiB, which stays free for anything that needs low memory |
| `pf_idx`, `pf_pa` | indexes `pf_n` and above are high frames; `hp_pa` finds the range by binary search |
| `pframe_free`, `pframe_ref` | handle both pools, so copy-on-write fork works on high frames |
| `pframe_free_count` | both pools |
| `pframe_hi_count`, `mm_hi_mapped_top`, `mm_ram_total` | for diagnostics |

The boot log line `memory: N MB RAM, F free frames, H above 4 GB` shows the result.

Limits: one PDPT (512 GiB); at most 32 high ranges; the 16-bit reference count.

## 3.15 The 100 MiB kernel layout

Since v9.22 the low memory map is:

| Range | Use |
|---|---|
| 1 MiB - 101 MiB | kernel image (`ASSERT(__file_end <= 0x06500000)` in `kernel64.ld`, `assert len(b) <= 104857600` in `build.sh`) |
| 152 - 154 MiB | boot stack (top `0x09A00000`, `boot64.S`) |
| 154 - 156 MiB | fallback for the 2 MB device window (`DMW_FIXED`, `include/nvx/mem/dmawin.h`). Since v9.81 `dmw_init()` normally takes the window from the DMA arena instead; offsets inside it: xHCI rings 0x10000, HID areas 0x80000, USB stick 0xD0000, AHCI 0x180000, identify buffers 0x1A0000, VirtualBox request 0x1A1000, NVMe 0x1B0000, PCnet 0x1D0000 |
| 176 - 192 MiB | DMA arena (unchanged) |
| 192 - 224 MiB | frame pool (unchanged) |
| 224 - 256 MiB | module area for the Doom WAD (unchanged) |
| 256 - 384 MiB | kernel BSS (`. = 0x10000000`, at most `0x18000000`) |
| 384 MiB - | heap (`heap_init` starts at `0x18000000`) |

Loaders:

| Loader | Change |
|---|---|
| myboot stage2 (BIOS) | reads the kernel file into a scratch area at `0x18000000` (was `0x1000000`), up to 100 MiB, then copies it to 1 MiB |
| UEFI loader | `KERNEL_MAX` 100 MiB; the loader's own buffer is allocated outside the kernel areas (`alloc_safe`); the reserved-area table and the BSS bounds follow the new layout |
| CD image (`mkhybrid.py`) | the El Torito UEFI FAT16 image doubles its sectors per cluster until it has fewer than 65,000 clusters; when it is larger than 65,535 sectors the catalog entry's sector count is 0, which UEFI firmware reads as "to the end of the disc" |

The frame pool now needs 500 MiB of RAM, because the heap it grows from starts at 384 MiB.
