NoovexOS 64 - v0.50  CUSTOM BOOTLOADER (experimental)
=====================================================

This drops the opaque Noovex9 stage2 blob and boots with a bootloader written
from scratch (mbr.S + stage2.S).  It has NO 1 MB kernel limit - it reads the
exact kernel size, so 5 MB / 50 MB kernels boot the same way.

WHAT IT DOES
------------
  MBR (LBA 0)      : BIOS loads it at 0x7C00, reads stage2 via INT 13h LBA.
  stage2 (LBA 1..) : enables A20; sets a VBE linear-framebuffer mode (tries
                     1024x768, 800x600, 640x480) and writes its params to
                     0x5000; enters "unreal mode" and streams the kernel to
                     0x100000 in 64-sector chunks; switches to 32-bit protected
                     mode and jumps to the kernel - exactly the state boot64.S
                     expects (flat segments, A20 on, EAX != multiboot magic).
  kernel (LBA 33+) : the normal NoovexOS 64 kernel, unchanged.

HOW TO BOOT IT  (raw hard-disk image, NOT a CD/floppy)
------------------------------------------------------
VirtualBox:
  - New VM, 128 MB+ RAM, chipset PIIX3, and ADD THE .img AS A HARD DISK:
    Settings -> Storage -> SATA/IDE controller -> Add Hard Disk ->
    "Add" -> pick  "Noovex64 v0.50 custom-boot.img".
    (VirtualBox may ask to convert .img to VDI - either convert, or use
     `VBoxManage convertfromraw "...custom-boot.img" nvx.vdi --format VDI`.)
  - Boot.  You should see the NoovexOS desktop.

Real hardware / USB:
  - Write the .img to a USB stick with dd / Rufus (as a raw disk image), boot
    the machine in Legacy/CSM (BIOS) mode from that USB.

IF IT WORKS
-----------
Tell me and I'll (a) embed the FreeDoom WAD in the image so DOOM works with no
separate disk, and (b) we can push the kernel to 50 MB.

IF IT FAILS  (then just go back to v0.49)
-----------------------------------------
Photograph what you see - the exact text tells me where it broke:
  "MBR disk err"   -> INT 13h LBA read of stage2 failed (controller/drive).
  " VBE fail"      -> no linear-framebuffer VBE mode accepted (video BIOS).
  " kern load err" -> INT 13h read of the kernel failed mid-load.
  black screen / reboot loop / hang -> likely unreal-mode copy or the PM jump.
Any of these is fixable, but needs your photo since I can't boot it here.

NOTE
----
- DOOM needs its WAD; this image doesn't carry one yet, so DOOM will say it
  can't load the WAD until I add WAD embedding (see "IF IT WORKS").  Everything
  else (desktop, browser with WebP, apps) runs.
- The splice-based v0.50 .iso / USB.img are still here as the proven fallback,
  identical boot behaviour to v0.49.
