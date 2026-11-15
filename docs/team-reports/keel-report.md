# Keel report - stability fixes (branch keel, commit 64b0ab23)

## Done
1. **HDA dead codec (scope #4)** - drivers/sound/hda.c
   - `verb()` counts consecutive timeouts (`vfail`). After 2 timeouts the per-verb wait drops from 200 ms to 10 ms. On a timeout the RIRB read pointer is resynced to the write pointer, so a late answer cannot shift every later response by one.
   - `hda_poll()`: once there are 3 or more consecutive timeouts, the jack check runs every 30 s instead of every 300 ms. "codec stopped answering" is logged once, and "codec answers again" is logged when it recovers. `vfail` is reset per codec in `hda_pick_codec` and in `hda_probe`, so a dead codec does not shorten the wait for the next one.
   - Worst case per jack poll for a dead codec goes from about 200 ms every 300 ms to 10 ms every 30 s.
2. **Stuck EC (scope #7)** - drivers/acpi/acpi_dev.c
   - The circuit breaker now backs off exponentially. The first trip still comes after 8 timeouts (5 s dead, as before). While the EC stays stuck, each retry is a short probe: a 2000-iteration wait instead of 20000, and 2 timeouts trip the breaker again. The dead time doubles each trip (5, 10, 20, 40, 80, 160 s, then capped). A one-time klog is written at the 3rd trip. Any successful read, write or query resets the back-off (`aec_good()`).
   - The struct in acpi_dev.h is unchanged; the new state is a file-static `aec_streak`.
3. **ARP `continue` skipped the timeout (scope #6)** - drivers/storage/ahci.c `net_poll`, net/stack/tcpip.c `nd_poll`
   - Changed to `if(n>0&&!arp_auto_reply(rx,n))return n;`, which then falls through to the PIT check. Behaviour is otherwise the same.
4. **BARs above 512 GB mapped only into the current CR3 (probe #7)**
   - Checked first: `vm_new_pml4()` (mem/pmm_vmm.c:141) copies all 512 PML4 entries of the current CR3 except slot 8 (user). Kernel slots therefore share PDPTs, but only for slots that existed when the space was cloned. APs load the BSP CR3 (smp.c:78). Process teardown frees only slot 8.
   - Fix: new `acpi_hi_prep()` in acpi_dev.c creates empty PDPTs for PML4 slots 1-7 in the boot page tables. It is called from core/init/main.c right after `pframe_init()`, before any address space exists, at a cost of 7 DMA-arena pages. Every later space then shares the same slot 1-7 PDPTs, so the PDPT/PD entries that `acpi_hi_map` adds from any CR3 (for example lazy `snd_init` from a Linux ioctl) are visible everywhere. `acpi_hi_map` is otherwise unchanged and still creates a slot itself if prep failed.

## Tests
- `/home/claude/ccheck.sh` on all 5 touched C files: no new warnings. I diffed the warning lists against HEAD; they are identical, and all are old -Wmisleading-indentation warnings.
- drvsim (all MODS from build.sh plus tools/tests/drivers/drvsim.c, ASan+UBSan): ALL PASS. New check: "HDA: codec that stops answering mid-play: jack polling backs off (12 s of polls cost < 1 s)". I confirmed it FAILS against the old hda.c.
- AML group (`check.g_aml`, run directly): 68 checks, all ok. 2 are new: the stuck EC backs off (short probe, then 10 s of silence), and an EC that answers again gets the full wait back.
- ACPI power group (`check.g_acpi`): ok.

## Untested
- Real hardware. The PML4 pre-creation has not been boot-tested (the lead's build plus QEMU should confirm boot is unaffected). The ARP fix has no host test.

## xHCI control-transfer timeout (description only; Tracker/Typist own controller_init.c)
In `xh_ctrl` (controller_init.c ~197), `if(!ok)return 0;` leaves the Setup/Data/Status TRBs live on the EP0 ring. The controller can run them later, and a late completion can then be taken as the answer to the next control transfer, because matching is only by slot and endpoint. Suggested fix on `!ok`:
1. `xh_cmd_s(0,0,0,15u|(1u<<6),cur_slot,&c2,&s2)` (Stop Endpoint, EP ID 1). If cc != 1 (for example 19, Context State Error, because the endpoint is Halted), send Reset Endpoint `14u|(1u<<6)` instead, the same as the cc==6 path.
2. `xh_cmd_s((R+e0_enq*16)|e0_cyc,0,0,16u|(1u<<6),cur_slot,...)` (Set TR Dequeue to the current enqueue pointer) so the stale TRBs are skipped.
3. Drop any `xh_q` entries for (cur_slot, ep 1) that the Stop produced (cc 26/27 transfer events) so they are not taken as later completions.
4. Optional, stronger: remember the Status TRB physical address and, in the wait loop, accept only a transfer event whose `e[0]` equals it.
The cc==6 path could share this helper (stop/reset, then set dequeue).

## Open
- scope #5 (synchronous USB hot-plug enumeration) is out of scope here.
