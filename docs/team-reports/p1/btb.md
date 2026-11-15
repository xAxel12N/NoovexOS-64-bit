Task btb: Broadcom BCM20702/BCM43xx USB Bluetooth (0A5C:21E8, 0A5C:216F, 0A5C:21xx, 0B05:17CB etc.) patchram in drivers/bluetooth/.
Source: Linux drivers/bluetooth/btbcm.c (fetch, verify SPDX is GPL-2.0-or-later) — HCI Download Minidriver (0xfc2e), write-RAM
records from the .hcd file, Launch RAM (0xfc4e), reset, then read local name/version. Hook into btusb.c with a 1–3 line call into a new
file bcmbt_fw.c (bti and btm coders hook there too). Inert for other devices. Firmware from /lib/firmware/brcm/BCM*.hcd (these are NOT
in linux-firmware and not redistributable — never bundle). Without firmware the chip still works with its ROM firmware: make that the
fallback with a message. Host test for the .hcd record parser.
