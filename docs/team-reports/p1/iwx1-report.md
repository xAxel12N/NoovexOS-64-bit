# iwx1 report (p1): Intel AX2xx PCIe transport and firmware loader

Branch p-iwx, commit 0eea1720 in /home/claude/wt/iwx. Only my three files are committed.

## Files
- drivers/net/wifi/iwx/iwx.h: the shared header. It has the CSR/PRPH registers, the context-info structs (gen2 and gen3), the TFD and RX descriptors, `struct iwx_softc`, and the API. It includes iwx2's iwx_cmd.h and does not redefine any of iwx2's macros.
- drivers/net/wifi/iwx/iwx_fw.c: picks the firmware for each PCI ID (the OpenBSD attach table), parses the TLV file (checks magic 0x0a4c5749), and loads the file from disk.
- drivers/net/wifi/iwx/iwx_pcie.c: PCI match and probe, MMIO, NIC lock, PRPH access, prepare_card_hw, sw_reset, apm_init, start_hw, DMA rings, gen2 and gen3 context info, the polled ALIVE wait, RX polling, host-command TX, data TX, MAC address from CSR, and `iwx_status()`.

## Licence
- Ported from OpenBSD if_iwx.c rev 1.230. I fetched the upstream raw file and checked that its ISC header matches the local copy.
- The struct and register definitions come from if_iwxreg.h, which is dual BSD/GPLv2 (Intel). It is used under BSD.
- The licence header block is the same one iwx2 uses (ISC plus the Intel BSD-3 text).
- **NOTICE entry still needed:** I did not add an entry to a NOTICE file. There is none under drivers/net/wifi yet. The lead should add one, for example drivers/net/wifi/iwx/NOTICE, covering both coders' files.
- No firmware blobs are committed.

## Behaviour and safety
- The driver does nothing unless a PCI device is vendor 0x8086 with one of 18 AX200/AX201/AX210/AX211/BZ IDs. The ID list comes from OpenBSD pcidevs.
- The RTL8821CE path is untouched. No existing file was changed, and kernel64.c and the shell were not touched.
- Every wait has a bound:
  - NIC lock: 150 ms
  - prepare_card_hw: about 1.75 s worst case
  - clock: 25 ms
  - ALIVE: 2 s
- The card runs polled. The INTx-disable bit is set in the PCI command register and `CSR_INT_MASK` is set back to 0 after boot, so there is no interrupt storm.
- A BAR above 4 GB is refused with a clear message, the same as rtw88 does.
- Missing firmware is logged once by klog with the exact path, for example `/lib/firmware/iwlwifi-cc-a0-77.ucode`.
  - The loader tries API versions from (OpenBSD version + 12) down to 50 (22000) or 59 (AX210).
  - For 8086:2725 it also tries the `ty-a0-gf-a0` firmware name.
- Unknown TLVs are skipped. OpenBSD rejects them, but Linux firmware files carry newer ones.
- DMA memory is allocated once and only after an ID match. dma_alloc_pages is a bump allocator, so retries reuse the same buffers.
- RX uses 64 buffers of 4 KB mapped onto the 512-entry ring (index i uses buffer i&63). The write pointer is never set more than 56 entries ahead, so a buffer is not reused before it has been read. This saves about 2 MB of DMA memory.
- Command queue: 256 TFDs that share 32 command buffers of 4 KB each.

## API for iwx2 (as declared in iwx_cmd.h)
- `iwx_get_sc` returns non-null only once the firmware is alive.
- Also provided: `iwx_tx_hcmd`, `iwx_tx_data`, `iwx_rx_poll`, `iwx_is_ax210`, `iwx_fw_api`, `iwx_fw_capa`, `iwx_hw_mac`, `iwx_now_ms`.
- `iwx_rx_poll` calls iwx2's `iwx_rx_pkt(sc,pkt,len)` for every packet, including ALIVE.
- Ring addresses for the SCD queue-config command are in `sc->txq[1].desc.pa` and `sc->txq[1].bc.pa`.
- Command versions from the firmware are available through `iwx_cmd_version()` and `iwx_notif_version()`.
- `iwx_tx_data` refuses to send until `sc->state` is `IWX_ST_READY`. iwx2 sets that state after enabling the data queue.

## Tested
- Both files pass ccheck.sh with no warnings.

## Not tested
- Nothing has been run on hardware or in an emulator. The owner has no Intel card, so all of this is "works in theory".
- There is no host test yet.
- I did not write static asserts for the struct sizes. They are worth adding: `iwx_ctxt_info`, `iwx_prph_scratch` and `iwx_ctxt_info_gen3` must match Linux and OpenBSD byte for byte.

## Not done / follow-ups
1. **Wiring (needs the lead):**
   - Add `#include` lines for the iwx .c files in kernel64.c, after rtw8821ce.c.
   - Add a shell command `iwx status` that calls `iwx_status(buf,cap)`. Starting the driver needs a call to `iwx_start()`, for example from `iwx up`.
   - wifi_mgr's `rtw_find_mmio` returns 0 for Intel cards, because `rtw_chip_select` fails. So the two drivers do not clash, but the wifi_hw_ops glue has to choose between them.
2. **AX210 PNVM is not loaded** (`iwx_load_pnvm`). AX210/AX211 firmware waits for PNVM after ALIVE, so it must be ported before the NVM, PHY and scan commands will work on those cards. AX200/AX201 do not need it.
3. Functions not ported:
   - `iwx_set_ltr`, needed on some integrated 22000 parts
   - `iwx_nic_config` (radio config bits on 22000)
   - the 22000 integrated force-power-gating step in `start_hw`
   - `iwx_post_alive`
   - `iwx_find_device_cfg`, which picks firmware by subsystem ID, so the choice between so and ty firmware on AX210 is a fallback rather than exact
4. The ALIVE detection by bit 0 of the MSI-X cause register assumes non-MSI-X mode, where the legacy `CSR_INT` latches. If neither register shows ALIVE, the driver restocks the RX ring anyway after 400 ms.
5. There is no stop or reset path (`iwx_stop_device`). After a failure the state stays failed until reboot.
