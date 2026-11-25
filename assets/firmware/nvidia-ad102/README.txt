NVIDIA Ada (AD102/AD103/AD104/AD106/AD107, RTX 40) GSP firmware used by drivers/video/nvidia/nvada.c.

These files are NVIDIA firmware from the linux-firmware project, xz-compressed:
  nvfw_ad102_booter_load.xz    nvidia/ad102/gsp/booter_load-535.113.01.bin
  nvfw_ad102_booter_unload.xz  nvidia/ad102/gsp/booter_unload-535.113.01.bin
  nvfw_ad102_bootloader.xz     nvidia/ad102/gsp/bootloader-535.113.01.bin
linux-firmware links nvidia/ad103, ad104, ad106 and ad107 to ad102, so the same files serve every Ada chip.
The GSP-RM image nvidia/ad102/gsp/gsp-535.113.01.bin is a link to nvidia/ga102/gsp/gsp-535.113.01.bin;
it is the Ampere GA10x image (assets/nvidia/nvfw_ga102_gsp.xz when present), or a copy placed in Files
as ga102-gsp-535.113.01.bin.

They are redistributable binary firmware under NVIDIA's licence in linux-firmware (WHENCE: "Licence:
Redistributable. See LICENCE.nvidia for details."). A copy of that licence is in LICENCE.nvidia next
to this file. The files are shipped unmodified (xz compression is lossless). They are not covered by
the NoovexOS licence.

A file with the name ad102-booter_load-535.113.01.bin, ad102-booter_unload-535.113.01.bin or
ad102-bootloader-535.113.01.bin placed in Files is used instead of the built-in copy.
