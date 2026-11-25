# iwx2 report (p1)

Commit 44c57f62 on branch p-iwx (worktree /home/claude/wt/iwx). I committed only my own files.

## What was done
Ported from OpenBSD if_iwx.c rev 1.230 and if_iwxreg.h rev 1.75. Licence: ISC, plus the Intel dual BSD/GPLv2 parts, used under BSD-3. I checked the licence headers in the real files fetched from github openbsd/src. The full header is in every file, and there is an entry in drivers/net/wifi/iwx/NOTICE.
- `drivers/net/wifi/intel/iwx/iwx_cmd.h`: the wire structs I need (rx_packet, NVM_GET_INFO rsp v3/v4, mcc, scan_config, scan_req_umac_v14 and its parts, scan_complete, rx_mpdu_desc v1/v3, tx_cmd gen2/gen3), the command IDs, the API/capa bits (checked against if_iwxreg.h), and the API in both directions.
- `iwx_cmd.c`:
  - Synchronous host commands (`iwx_send_cmd_sync`). There is one wait slot, matched on the wide id and on non-unsolicited qid. Every wait has a ms timeout, a timeout is logged only once, and REPLY_ERROR maps to -2.
  - RX packet dispatch `iwx_rx_pkt`, handling ALIVE, RX_MPDU, SCAN_COMPLETE_UMAC and REPLY_ERROR.
  - `iwx_nvm_init` (NVM_GET_INFO v3/v4: MAC from the transport, bands, antennas, LAR, channel list).
  - `iwx_init_hw_cmds`: tx_ant, BT config, DQA, power, MCC "ZZ" when LAR is on, SCAN_CFG and beacon filter. These follow iwx_init_hw, minus soc_conf, LTR, temp and phy_cfg.
- `iwx_scan.c`: UMAC scan v14 (SCAN_REQ_UMAC 0x10d), passive unless an SSID is given. It builds a probe request and channel config v2 from the NVM channels, with defaults if the NVM list is empty. A BSS table (48 entries) is filled from beacons and probe responses (SSID, DS channel, RSSI, capab). Completion comes from SCAN_COMPLETE_UMAC.
- `iwx_data.c`: RX MPDU parsing (desc v1 48 B or AX210 v3 64 B, CRC/overrun checks, 2-byte pad flag, RSSI as max(-energy_a, -energy_b)). Frames are sent to the scan parser and to `iwx_rx_deliver`. TX builds a gen2/gen3 tx cmd at a fixed 6M rate on antenna A and calls the transport's `iwx_tx_data`.
- `iwx_glue.c`: the `wifi_hw_ops` adapter for the hwops header as it currently stands in /home/claude/wt/hwops (include/nvx/net/wifi/wifi_hw.h). The wiring point is `iwx_glue_register()`. It returns -1 if `iwx_get_sc()` is NULL, so it does nothing without the hardware. RX goes through `wifi_hw_rx`.
- `tools/tests/iwx/iwx_host.c`: host test with ASan+UBSan. It mocks the transport and covers struct sizes, a sync command plus timeout, scan command id and size, RX MPDU for both desc versions into the BSS table, scan completion, malformed packets, and TX. Build and run:
  `gcc -fsanitize=address,undefined -I. tools/tests/iwx/iwx_host.c drivers/net/wifi/iwx/iwx_{cmd,scan,data}.c && ./a.out`
  Result: OK.

## Contract the transport (iwx1) must provide (declared extern in iwx_cmd.h)
I could not reach iwx1, so the lead needs to reconcile these names with iwx.h:
`struct iwx_softc* iwx_get_sc(void)` (NULL if no device or not alive), `int iwx_tx_hcmd(sc,u32 wide_id,const void*,u32)`, `int iwx_tx_data(sc,int qid,const void* txcmd,u32 tclen,const void* frame,u32 flen)` (qid -1 = default/mgmt queue), `void iwx_rx_poll(sc)` (must call my `iwx_rx_pkt(sc,pkt,len)` for each RX buffer), `int iwx_is_ax210(sc)`, `int iwx_fw_api(sc,bit)`, `int iwx_fw_capa(sc,bit)`, `void iwx_hw_mac(sc,u8*)` (MAC from CSR/OTP), `u64 iwx_now_ms(void)`. `iwx_glue.c` needs wifi_hw.h from the hwops branch, so it only compiles after that merge. It compiled clean with `-I/home/claude/wt/hwops`. The other three .c files pass ccheck.sh with no warnings.

## Not done / untested (honest status: "scan works in theory")
- Nothing has run on real hardware. The owner has no Intel device, and none of this code was exercised against firmware.
- Join is a stub returning -1 with a one-time message. Still missing: MAC context, PHY context (cmd v3/v4/uhb), binding, ADD_STA, session protection, TLC/rs_init and enable_txq. set_key is also a stub (ADD_STA_KEY is not ported), so auth/assoc/WPA does not work yet.
- Only scan v14 is supported. It needs the SCAN_EXT_CHAN_VER API bit. Newer AX210/AX211 firmware that requires scan v15/v16/v17 (scan_umac_v17 with general_params_v11 and channel_cfg v5) will be refused cleanly with a message. That is the next item to port. Scan priority uses EXT_6 = 6.
- The NVM channel_profile index-to-channel map is a simplified 39-entry 2.4/5 GHz table. There is no 6 GHz support.
- TX: there is no TX_CMD response handling, no QoS/data queues per TID, and no encryption offload.
- `scan_start` in the glue ignores the channel list wifi_mgr passes in.

## Follow-ups
1. The lead reconciles the transport names with iwx1's iwx.h, adds the iwx_*.c files to units.list, and calls `iwx_glue_register()` after ALIVE.
2. Port scan v17 plus PHY/MAC/binding/STA/session-protection for join, then ADD_STA_KEY for CCMP.
