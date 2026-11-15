# s3: permissive Bluetooth host stacks above HCI

## What we have
drivers/bluetooth/: btusb.c (197 lines), hci.c (154), hci_parse.h (53), rtlbt_fw.c/.h (Realtek firmware loader for 0BDA:C123). That is HCI transport, firmware load and event parsing. No L2CAP, SDP, RFCOMM, SMP, GATT, A2DP/AVRCP or HID host anywhere in the tree (grepped for l2cap|rfcomm|gatt|nimble|btstack).

## Sources

| # | Stack | URL | Licence (verified from header) | Scope | Size | Bare-kernel effort | Use 1-5 |
|---|---|---|---|---|---|---|---|
| 1 | Zephyr BT host (incl. host/classic/) | github.com/zephyrproject-rtos/zephyr subsys/bluetooth/host | `SPDX-License-Identifier: Apache-2.0` (l2cap.c, classic/rfcomm.c) | LE: L2CAP, ATT/GATT, SMP (LE SC). Classic in host/classic/: br.c 39K, l2cap_br.c 210K, ssp.c 23K, sdp.c 107K, rfcomm.c 69K, avdtp.c 72K, a2dp.c 44K, avrcp.c 139K, hfp_hf.c 107K, hid_host.c 49K (bytes) | host roughly 60-80k lines C | Medium-high. Depends on Zephyr kernel API (k_work, k_sem, k_fifo, net_buf, k_mem_slab), Kconfig macros and devicetree. Needs a shim of about 1-2k lines (work queue, semaphores, net_buf, settings stub) plus a generated config header. Crypto: needs AES-CMAC/P-256 ECDH (TinyCrypt or PSA, both permissive) | 5 |
| 2 | Apache NimBLE host | github.com/apache/mynewt-nimble nimble/host | Apache-2.0 (ASF header in ble_l2cap.c) | BLE only: L2CAP (incl. CoC), ATT/GATT client+server, SMP (legacy + SC), GAP. No BR/EDR, so no A2DP/AVRCP/RFCOMM/classic HID | host about 40-50k lines | Low-medium. Built to be ported: porting/npl/ abstraction (event queue, mutex, sem, callout, ~20 functions), existing ports for FreeRTOS, Linux, RIOT. HCI transport is a small pluggable layer. Uses TinyCrypt (BSD) for SMP | 4 (LE only) |
| 3 | Google Bumble | github.com/google/bumble | Apache-2.0 (LICENSE) | Full host in Python: L2CAP, SDP, RFCOMM, SMP, GATT, A2DP, AVRCP, HID, HFP | Python | Not runnable in kernel; useful as a readable reference and as a test peer over a USB passthrough or HCI socket from a dev box | 3 (reference) |
| 4 | Android Fluoride / Gabeldorsche | android.googlesource.com/platform/packages/modules/Bluetooth | Apache-2.0 (AOSP) | Complete classic+LE, all profiles | very large (hundreds of k lines C++ plus Rust) | Very hard. C++17, libbase, libchrome, flatbuffers, Rust (gd/rust), Android HAL, binder. Not practical on our kernel | 1 |
| 5 | BTstack | github.com/bluekitchen/btstack | REJECTED. BSD-3 plus clause 4: "Any redistribution, use, or modification is done solely for personal benefit and not for any commercial purpose..." Not open source; incompatible with GPLv3 | Would otherwise be the best fit (full classic+LE, tiny run loop, bare-metal ports) | about 100k lines | Easy port, but licence forbids | 0 (code); facts only |

## Notes
- Classic profiles (A2DP/AVRCP/RFCOMM/HID host over BR/EDR) under a permissive licence: Zephyr is the only realistic C option. Zephyr classic is younger than its LE side; A2DP needs an SBC codec (Zephyr ships a lib, or use Apache-2.0 libsbc from Android/Bumble ref, or a GPL-2.0-or-later one).
- HID host for keyboards/mice: BLE HoG can be done on NimBLE or Zephyr GATT; classic HID host is in Zephyr classic/hid_host.c.
- Pairing keys need persistent storage; both stacks expect a settings/kv backend, which we can stub to a file on our VFS.

## Top 3
1. Zephyr BT host (Apache-2.0): only permissive C stack with LE plus classic (SDP, RFCOMM, A2DP, AVRCP, HID host, SSP). Costs an OS shim for k_work/net_buf/Kconfig.
2. Apache NimBLE host (Apache-2.0): quickest way to get LE GATT, SMP and HoG running; clean NPL port layer. No classic.
3. Google Bumble (Apache-2.0): Python reference for protocol flows and a test peer; no kernel code.
BTstack is rejected (non-commercial clause). Fluoride/GD is too heavy.
