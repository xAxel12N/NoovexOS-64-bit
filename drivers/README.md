# drivers

Hardware drivers, one folder per kind of device.

| Folder | What is in it |
|---|---|
| `acpi/` | AML interpreter, ACPI devices, laptop hot keys and battery |
| `bus/` | PCI |
| `i2c/` | DesignWare I2C |
| `input/` | `input.c` (shared input state); `ps2/` keyboard, mouse and PS/2 touchpads; `touchpad/` I2C-HID touchpads and gestures; `vm/` the VirtualBox/VMware mouse |
| `net/` | `ethernet/` (PCnet, e1000, RTL8139/8169) and `wifi/` (RTL8821CE) |
| `rtc/` | CMOS clock |
| `sound/` | HD Audio and AC97 |
| `storage/` | ATA, AHCI, NVMe |
| `usb/` | xHCI with HID, mass storage, hubs and cameras |
| `video/` | `fb/` framebuffer and compositor back end, `intel/` Intel iGPU, `nvidia/` NVIDIA (probe, EDID, GSP-RM) |
| `virtio/` | VirtIO net and block |
