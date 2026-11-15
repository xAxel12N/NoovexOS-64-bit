# iwxj report (q1): iwx join, CCMP keys, AX210 PNVM

The work is in commit 300ac49f on branch p-iwxi (/home/claude/wt/iwxi), on top of the integrator's 11d7f41c. I committed only my own files: iwx_cmd.c, iwx_data.c, iwx_scan.c and the new iwx_pnvm.c, all under drivers/net/wifi/iwx/.

The code is ported from OpenBSD if_iwx.c rev 1.230 and if_iwxreg.h, which are under ISC, with the Intel portions used under their BSD-3 licence. iwx_pnvm.c has the same licence header as the other iwx files, and the integrator's NOTICE already lists it.

## What was done
- **iwx_pnvm.c, the AX210/AX211 PNVM loader (`iwx_load_pnvm`)**
  - The SKU id comes from the ALIVE v5/v6/v7 payload at offset 116, which iwx_cmd.c captures.
  - If the SKU id is all zero, the loader skips PNVM, as OpenBSD does.
  - It prefers the PNVM data embedded in the firmware file (TLV 74, `sc->pnvm_tlv`). Otherwise it reads `/lib/firmware/iwlwifi-<pnvmbase>.pnvm`.
  - It looks for the section whose SKU id matches, then checks HW_TYPE against the mac type and rf id. It collects the SEC_RT segments (at most 64, at most 1 MB in total).
  - The image goes into DMA memory, either contiguous or as a fragmented table when firmware capability bit 32 is set. `pnvm_base_addr`/`pnvm_size` are then written into the prph scratch.
  - It rings `UREG_DOORBELL_TO_ISR6` (bit 20, at the UMAC offset) while holding the NIC lock, and waits up to 2 s for PNVM_INIT_COMPLETE (0xc/0xfe).
  - If the file is missing, it logs the exact path once and still rings the doorbell, so the firmware continues without PNVM (what Linux does).
  - It runs only once per firmware boot.
- **Called from `iwx_init_hw_cmds` before NVM_GET_INFO.** The glue's `init` runs this, so no new wiring is needed.
- **iwx_pnvm.c also exports small transport helpers** that need the softc: `iwx_txq_info`, `iwx_txq_reset`, `iwx_set_ready` and `iwx_use_mld`.
- **iwx_cmd.c `iwx_join` / `iwx_join_ssid`**, following the order in OpenBSD `iwx_auth`/`iwx_run`:
  1. PHY ctxt add (cmd v3/v4, the UHB variant when capability bit 48 is set), plus RLC_CONFIG v2 when the firmware needs it.
  2. MAC ctxt add (BSS_STA, EDCA defaults, ACCEPT_GRP|IN_BEACON).
  3. Binding.
  4. ADD_STA (sta 0, LINK).
  5. TX queue enable on qid 1, TID 15. It uses TX_QUEUE_CFG v0 or SCD_QUEUE_CONFIG v3, and checks that the queue number and write pointer in the reply are as expected.
  6. Session protection of 900 TU.
  7. State is set to READY.
  8. Auth (open system, 3 tries of 300 ms each).
  9. Assoc request with SSID and legacy rates (3 tries of 500 ms each).
  10. ADD_STA modify, and MAC ctxt modify with is_assoc and the AID.
  - Return codes follow wm_err: 1 = no auth reply, 2 = refused, 3 = no assoc reply, -1 = driver failure.
  - Every failure tears everything down again in order: queue (v3), REMOVE_STA, binding, MAC, PHY.
  - `iwx_leave(sc)` sends a deauth, removes the session protection and tears down.
  - The SSID comes from our own BSS table (new `iwx_scan_find` in iwx_scan.c), because `iwx_join()` in iwx_cmd.h has no ssid argument.
  - Join is refused with a single log line on firmware that uses the MLD API (MA type 0x7e40 or BZ). That path is not ported.
- **`iwx_set_key`**: ADD_STA_KEY v2 for CCMP-128 (type 2, 16 bytes), pairwise at key offset 0 and group at offset 1 with the MULTICAST flag, as OpenBSD `iwx_add_sta_key_cmd` does.
- **iwx_data.c**
  - **Fixed a real bug: TX could never work before.** `iwx_tx_frame` used qid -1, which `iwx_tx_data` always refuses.
  - TX now uses qid 1. It sets ENCRYPT_DIS (the host does CCMP in software), the MH_SIZE offload bits, and padding plus the PAD flag for 26- and 30-byte headers. It uses the rate_n_flags v2 legacy-OFDM encoding when the TX_CMD notification version is above 6.
  - RX now records auth, assoc and reassoc responses and deauth/disassoc from the target BSS for the join.
  - Once a key has been installed, frames the hardware decrypted with CCM (DEC_DONE|MIC_OK) are passed up without the 8-byte CCMP header and with the Protected bit cleared. The hardware has already stripped the MIC. Frames that failed decryption are dropped.

## Safety
- Every wait has a bound: commands 1-2 s, PNVM 2 s, auth/assoc at most 2.4 s.
- iwx_cmd.c reaches the iwx_pnvm.c helpers, `iwx_cmd_version` and `iwx_notif_version` through **weak references**. If a file is not linked, the feature logs once and is refused. This keeps the tools/tests/iwx host test linking without changes.
- No file outside drivers/net/wifi/iwx was touched. RTL8821CE and Realtek BT are not affected.

## Tests
- ccheck.sh on all 4 files: no warnings. `_Static_assert`s check the struct sizes: MAC ctx 148, PHY 28/32, STA key 76, SCD v3 36, RLC 32.
- `python3 tools/check.py --only wifi`: 16/16 passed, including the iwx host test.
- I did not run build.sh, as the brief says.

## Not tested / limits
- Nothing has run on hardware (there is no Intel card).
- Not ported: the TLC/rate-scaling config (all TX uses a fixed 6 Mbit/s with CMD_RATE), the HT/VHT station flags, QoS/AMPDU, the MLD API path, scan v15+, 6 GHz, and `iwx_post_alive`/SOC/LTR.
- Beacon interval and DTIM are assumed to be 100 TU.

## The integrator needs to
1. Add `drivers/net/wifi/iwx/iwx_pnvm.c` to units.list, next to iwx_data.c. Without it, PNVM and join are refused with a log message instead of breaking the build.
2. In iwx_glue.c:
   - `iwx_g_join`: call `int iwx_join_ssid(struct iwx_softc*,const u8* bssid,int ch,const char* ssid)` with the ssid argument, so hidden networks and an empty table work.
   - `iwx_g_leave`: call `void iwx_leave(struct iwx_softc*)`.
   - Add both prototypes to iwx_cmd.h.
3. If `set_key` is ever called by the host (it is not today): after that, RX delivers decrypted, unprotected frames, while TX still expects frames the host has already encrypted.
