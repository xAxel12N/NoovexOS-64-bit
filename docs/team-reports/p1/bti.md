Task bti: Intel Bluetooth firmware loading for USB BT (8087:0025/0026/0029/0032/0033/0aa7/0aaa etc., AX200/AX201/AX210 BT parts) in
drivers/bluetooth/. Source: FreeBSD usr.sbin/bluetooth/iwmbtfw (BSD-2; verify): legacy (bootloader + .sfi/.ddc) and TLV (intel_version_tlv)
paths. Hook into btusb.c/hci.c the same way rtlbt_fw.c is hooked, keyed by USB ID; inert for everything else (owner's Realtek must
behave exactly as before). Firmware loaded from /lib/firmware/intel/ibt-*.sfi (do not commit blobs). Extend tools/tests/bluetooth with
a parser test using a synthetic TLV version reply. Coordinate: btm and btb coders also hook into btusb.c — keep your hook a 1–3 line
call into a new file intelbt_fw.c to make merging easy.
