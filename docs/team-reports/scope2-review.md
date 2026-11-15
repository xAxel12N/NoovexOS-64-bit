# Scope review 2: d2feee70..main (v11.00: Tracker, Typist, Anchor, Keel)

48 files, +447/-92. 38 of those are C files, and most of the C changes only bump the version string from v10.99 to v11.00. The real logic changes are in acpi_dev.c, hda.c, tcpip.c, ahci.c (net_poll), xhci controller_init.c and hid_keyboard_mouse.c, and wifi_mgr.c. I read all of them.

## 1. HIGH: acpi_hi_prep breaks vm_selftest isolation and leaves a dangling shared mapping
- core/init/main.c:242 now calls acpi_hi_prep(). drivers/acpi/acpi_dev.c:157-159 fills kernel PML4 slots 1..7 with shared (empty) PDPTs at boot.
- mem/pmm_vmm.c:141-144 (vm_new_pml4) copies every kernel PML4 entry into a new address space and only clears slot 8.
- mem/pmm_vmm.c:214 (vm_selftest) uses V=0x8000000000, which is PML4 slot 1. The comment there still says "untouched by the kernel map -> per-space". That is no longer true.
- What happens: vm_map(sp1,V,fa) creates a PD and PT inside the shared PDPT. vm_map(sp2,V,fb) then walks into the same tables and overwrites the same PTE. Both spaces read 0xBBBB2222, so vm_selftest returns 3 ("isolation FAILED"). The shell callers are shell/commands/accounts_files_info.c:263 and :299.
- Side effects:
  - The shared kernel PDPT slot 1, entry 0, is now a 4 KB PD/PT chain pointing at frame fb, and pframe_free(fb) then frees that frame. The result is a stale kernel-wide mapping of a freed frame.
  - acpi_hi_map(pa in 512 GB..513 GB) would see pdpt[0] present and return pa without mapping it correctly.
- User processes are safe: they use slot 8 only (ring3.c:60, elf.c ELF_IMG_LO, jc_uok).
- Fix: move vm_selftest's V into slot 8 (for example 0x47F00000000), or have acpi_hi_prep skip slot 1. Then update the comments at pmm_vmm.c:139 and :214.

## 2. MEDIUM: rlen can exceed wMaxPacketSize on HID interrupt IN
- drivers/usb/xhci/controller_init.c:322: `rlen = max(mps, inl)`, capped at 64.
- A full- or low-speed device with mps=8 whose longest report is 9 to 64 bytes now gets a TD longer than MPS. That is legal in xHCI: short packets end the TD.
- But `got = xhid_rlen - res` (hid_keyboard_mouse.c:80) only stays correct if the transfer event arrives on a short packet (ISP, or IOC with the residue reported).
- Please confirm xhid_arm sets ISP/IOC. If it does not, a 9-byte report on mps=8 could sit in flight until the next packet.
- The data buffer is fine: it is 0x4000 per slot.

## 3. LOW: hidkbd_test.sh leaks its temp dir on failure and ignores check.py settings
- tools/tests/usb/hidkbd_test.sh:2-6: with `set -e`, a failing hidkbd_t exits the script before `rm -rf "$T"`. The exit status is still non-zero, so check.py does see the failure.
- It always builds with `-fsanitize=address,undefined`. Every other test uses check.py's SAN list, which defaults to [] at check.py:13. On a host without libasan, this check fails where the others pass.
- It does not use check.py's `work` dir (TMPDIR).

## 4. LOW: EC back-off probe may be too short for a slow but alive EC
- drivers/acpi/acpi_dev.c:37: while aec_streak>0, every aec_wait uses 2000 iterations instead of 20000, including the first probe after the dead window ends.
- An EC that is slow (more than about 2000 io_waits) but alive keeps re-tripping, and the back-off grows to 5 s << 5 = 160 s.
- There is no overflow: the streak is capped at 6.
- The new aml_template test (lines 238-242) covers stuck-to-OK recovery, but not this slow-EC case.

## 5. LOW: array-style modifier usages also go through the key-down path
- hid_keyboard_mouse.c:33 now folds usages 0xE0-0xE7 found in d[2..7] into `mod`.
- The same usages still run through the normal press loop: hid2scan(k), kdown[sc]=1, usb_key_code. On release, the same loop sets kdown[sc]=0.
- Whether this matters depends on what hid2scan returns for 0xE0-0xE7. If it maps them, it can fight the mod-based kdown[0x1D/0x2A/0x38/0x5B] writes on line 35.

## Checked, no problems found
- **g_ticks arithmetic** (the timer runs at 100 Hz, main.c:260): wifi_mgr.c WM_STABLE (12000 ticks = 120 s), wm_fail_delay (30 s up to 300 s, then 600 s), wm_retry_note `/100` gives seconds, and wm_lost `g_ticks-g_wm_since`. All are u64-u64 with nothing negative. hda.c:156 is a u64 ns compare. aec dead_until is 5e9<<5 at most, no overflow.
- **Buffer sizes in wifi_mgr.c**:
  - wm_init_step st[64]: needs 37
  - wm_connect st[64]: needs 61
  - "Looking for" st[80]: needs 70
  - wm_retry_note st[96]/k[96]: needs 90/88 at most
  - wm_unhex(..., wm_last, 33): bounds-checked
  - "off=" / "last=" parsing: k>i+3 / k>i+4 guards are fine
- **HID changes**:
  - hid_inlen only counts Input items (rid is masked to 0xFF, so bitpos[256] is safe)
  - xhid_kprevx index clamp is correct
  - .bss growth is XHID_MAX*3*8 = 240 B, negligible
  - GET_PROTOCOL reads into &pr through xh_ctrl's data pointer
  - boot keyboard copy into kb[8] with got>=3 is safe
- **net_poll / nd_poll** (ahci.c:130, tcpip.c:26): the change fixes the old unbounded spin under an ARP flood. The time check now runs after ARP replies.
- **HDA**: rirb_rp is resynced on timeout. The dead-codec path logs once and polls every 30 s. vfail is reset in probe and in pick_codec.
- **Debug prints**: there are no new prints in per-frame or IRQ paths. The klogs in wifi_mgr are rate-limited by the back-off, and the HDA and EC messages print once per event.
- **Test wiring**: every new test file is run by tools/check.py.
  - mgr_gen.py and mgr_main.c: check.py:354-357
  - hidmouse_gen.py and hidmouse_main.c: :414-418
  - hidkbd_test.sh, which pulls in hidkbd_gen.py, hidkbd_env.h and hidkbd_main.c: :419-420
  - aml_template, usb_template and drvsim additions run inside the existing groups
- **Uninitialized variables**: none found. The new statics are zero-initialized, and the new locals (inl[6], kb[8]) are explicitly initialized.
