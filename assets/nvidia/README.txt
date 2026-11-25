NVIDIA GPU firmware used by the Turing GSP driver (drivers/video/nvidia/nvgsp.c).

These files are the NVIDIA firmware from the linux-firmware project, xz-compressed:
  nvfw_gsp.xz           nvidia/tu102/gsp/gsp-535.113.01.bin (GSP-RM, used for all Turing chips)
  nvfw_bootloader.xz    nvidia/tu102/gsp/bootloader-535.113.01.bin
  nvfw_booter_load.xz   nvidia/tu116/gsp/booter_load-535.113.01.bin
  nvfw_booter_unload.xz nvidia/tu116/gsp/booter_unload-535.113.01.bin
  nvfw_acr_bl.xz        nvidia/tu116/acr/bl.bin
  nvfw_ga102_gsp.xz           nvidia/ga102/gsp/gsp-535.113.01.bin (GSP-RM for Ampere GA102-GA107; linux-firmware links ga103-ga107/gsp and ad102/gsp-535.113.01.bin to it)
  nvfw_ga102_bootloader.xz    nvidia/ga102/gsp/bootloader-535.113.01.bin
  nvfw_ga102_booter_load.xz   nvidia/ga102/gsp/booter_load-535.113.01.bin
  nvfw_ga102_booter_unload.xz nvidia/ga102/gsp/booter_unload-535.113.01.bin

They are redistributable binary firmware under NVIDIA's licence in linux-firmware.
A copy of that licence is in LICENCE.nvidia next to this file, and in the installed
system at /usr/share/licenses/firmware/LICENCE.nvidia. The files are shipped unmodified
(xz compression is lossless). They are not covered by the NoovexOS licence.

A file with the original name (for example booter_load-535.113.01.bin) placed in Files
is used instead of the built-in copy, which is how a TU117-specific booter can be supplied.
For Ampere the Files names carry a ga102- prefix (for example ga102-booter_load-535.113.01.bin).
Ampere (RTX 30) is only booted after gpu ampere on; that path is untested on real hardware.
