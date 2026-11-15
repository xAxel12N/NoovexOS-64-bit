# Sentinel review 2: Keel stability changes (d2feee70..main)

Scope: drivers/sound/hda.c, drivers/acpi/acpi_dev.c, drivers/storage/ahci.c (net_poll), net/stack/tcpip.c, core/init/main.c. I only read code; nothing was built or run. The rest of the diff is the 10.99 -> 11.00 version-string bump.

## Overall verdict
acpi_hi_prep is sound for boot and for normal memory mapping. One real regression: the VM isolation self-test now fails, and it leaves a stale mapping that every address space shares (F1). The HDA and EC back-offs both recover by themselves. Neither can permanently turn off sound or battery after a single transient error. The EC back-off has a tuning risk (F2). The ARP poll fix is correct.

## Findings (ranked)

### F1. MEDIUM: acpi_hi_prep breaks vm_selftest and leaves a stale mapping in every address space
- acpi_hi_prep (drivers/acpi/acpi_dev.c:157-159) runs at boot (core/init/main.c:242). It fills PML4[1..7] with shared, present PDPT pages (flags 3).
- vm_new_pml4 (mem/pmm_vmm.c:141-145) copies all 512 entries, so slots 1-7 now point at the same PDPT pages in every address space. That sharing is what the change is for.
- vm_selftest (mem/pmm_vmm.c:209-225) uses V=0x8000000000 (line 214). The comment there says "PML4 slot 1 ... per-space". After this change that is no longer true. vm_map(sp1,V) and vm_map(sp2,V) go through vm_walk (pmm_vmm.c:158-163) into the same shared PDPT. They create one PD and PT with flags 7, so sp2's PTE overwrites sp1's.
  - Result: both reads return 0xBBBB2222 and the test returns 3. Both shell commands that run it report FAIL: shell/commands/accounts_files_info.c:263 ("[1-2] paging + isolation FAIL") and :299.
- What it leaves behind:
  - The test then calls pframe_free(fb). Every address space, kernel included, still maps VA 0x80_0000_0000 to that freed frame, and the frame can be reused (for example by a user process).
  - PDPT slot1[0] is now present and is not an identity mapping. A later acpi_hi_map(pa) with pa in [512 GB, 513 GB) sees pdpt[i3]&1 set (acpi_dev.c:166), skips building the 1 GB identity PD, and returns pa. MMIO at that address then hits the freed RAM frame or page-faults.
  - Exposure is narrow (the self-test must have been run, and a device BAR must sit in that 1 GB), but it is a correctness bug.
- Fix: in vm_selftest, move V into the per-space user slot, for example 0x47F00000000 (PML4[8], inside jc_uok's range, core/proc/jobs/resume_and_fds.c:28). Alternatively, make vm_walk refuse PML4 slots 1-7.

### F2. LOW-MEDIUM: EC back-off is too quick to give up on a healthy but busy EC, and backs off for up to 160 s
- drivers/acpi/acpi_dev.c:36-41. After the first trip (aec_streak >= 1):
  - The per-wait limit drops from 20000 to 2000 io_wait. io_wait is an outb to port 0x80, about 1 us (arch/x86_64/cpu/io.h:4), so the limit is about 2 ms.
  - Only 2 failed transactions are needed to trip again.
  - The back-off doubles each trip: 5 s << (streak-1), capped at streak 6, which is 160 s.
- Laptop ECs can be slow for several milliseconds during SMI or thermal events (Linux allows 500 ms per transaction). A healthy machine that hits one burst of 8 slow transactions enters "short-timeout mode". From then on, every 2 slow (>2 ms) transactions escalate the back-off.
- It recovers only if a transaction finishes within 2 ms. It cannot get stuck forever: any success calls aec_good() and resets fails and streak (line 35). But on a slow EC the battery and AC readings can stay stale for minutes at a time.
- During a back-off window, AML reads in EC space return -1 (acpi_dev.c:108). aml_sta treats an eval error as 0x0F (drivers/acpi/aml.c:489), so the battery does not vanish, but _BST data goes stale.
- Suggestion: keep the full 20000 limit for the first wait after each back-off window, so the probe is fair. Alternatively, cap the back-off at about 30 s. Make the streak decay with time as well as on success.

### F3. LOW: HDA back-off recovers by itself; some small cosmetic and latency points
- drivers/sound/hda.c:17-26 and 154-156.
- After 2 consecutive verb timeouts, the timeout drops from about 200 ms to about 10 ms. After 3, jack polling slows to every 30 s.
- Any successful verb sets vfail=0 (line 24), so a transient error cannot permanently disable sound. Volume, stream setup and jack polling all keep trying.
- vfail is reset per codec at hda_pick_codec (line 210) and at probe.
- Resyncing rirb_rp on timeout is an improvement: it stops later responses from being taken as answers to the wrong verbs. A response that arrives after the resync can still be mismatched to the next verb, but that is rare.
- Cosmetic: if a verb outside hda_jack succeeds first, vfail goes to 0 without going through the "answers again" branch. vdead_log then stays 1, and the next "codec stopped answering" message is not logged.
- Latency: a healthy codec that is slow on a power-state verb (705h on D3 to D0) right after 2 failures gets only 10 ms. It may fail once more and then recovers on the next verb.

### F4. INFO: acpi_hi_prep is safe on the other points checked
- **User space:** user space is PML4[8] only. jc_uok allows 0x400_0000_0000..0x480_0000_0000 (resume_and_fds.c:28), and ELF and mmap bases are 0x400xxxxxxxx-0x47xxxxxxxxx. No user mapping lands in slots 1-7, and their missing U bit does not matter.
- **Fork/teardown:** these touch only pml4[8] (core/proc/sched.c:149, resume_and_fds.c:115-134), so the shared PDPTs are never freed or copied.
- **Kernel map:** the kernel uses PML4[0] for identity and the high-memory map (pmm_vmm.c:70-84, uefiboot.c:95). Slots 1-7 were unused, and prep fills only entries that are not present.
- **SMP:** APs get the BSP's CR3 (arch/x86_64/smp/smp.c:78-79), and SMP starts later (main.c:292).
- **DMA window:** dmw_init runs at main.c:213, before prep, so the DMW base is unchanged. Prep uses 7 pages of the 16 MB DMA arena. If the arena were exhausted, prep returns early and leaves the remaining slots on the old behaviour (per-space creation in acpi_hi_map). No crash.
- **Walkers** (vm_skip, kva_to_pa, igpu, swap): they now descend into the empty PDPTs in slots 1-7 instead of skipping them. That is harmless; the Linux walkers operate on user ranges in slot 8.
- **The fix itself:** before this change, acpi_hi_map called after processes existed created PML4 slots only in the current space. Pre-creating the slots makes later PDPT entries visible in all spaces, so the change does what it is meant to do.

### F5. INFO: ARP poll fix is correct
- drivers/storage/ahci.c:130 (net_poll) and net/stack/tcpip.c:26 (nd_poll).
- Before, a frame answered by arp_auto_reply did `continue` and skipped the PIT deadline check, so an ARP flood could spin forever.
- Now ARP frames fall through to the timeout check, and every other frame is returned. Behaviour is otherwise unchanged.
