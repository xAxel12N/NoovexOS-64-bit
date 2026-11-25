# Bluetooth drivers

- `transport/`: HCI over USB (`btusb.c`).
- `firmware/`: vendor firmware loaders, one per vendor: Realtek (`rtlbt_fw.c`), Intel (`intelbt_fw.c`), MediaTek (`mtkbt_fw.c`), Broadcom (`bcmbt_fw.c`).

The Bluetooth host stack (HCI core, connections, L2CAP, pairing, profiles) is in `net/bluetooth/`.
