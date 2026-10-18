NVIDIA GPU firmware used by the Turing GSP driver (drivers/video/nvidia/nvgsp.c).

These files are the NVIDIA firmware from the linux-firmware project, xz-compressed:
  nvfw_gsp.xz           nvidia/tu102/gsp/gsp-535.113.01.bin (GSP-RM, used for all Turing chips)
  nvfw_bootloader.xz    nvidia/tu102/gsp/bootloader-535.113.01.bin
  nvfw_booter_load.xz   nvidia/tu116/gsp/booter_load-535.113.01.bin
  nvfw_booter_unload.xz nvidia/tu116/gsp/booter_unload-535.113.01.bin
  nvfw_acr_bl.xz        nvidia/tu116/acr/bl.bin

They are redistributable binary firmware under NVIDIA's licence in linux-firmware
(the file LICENCE.nvidia in that project). They are not covered by the NoovexOS licence.

A file with the original name (for example booter_load-535.113.01.bin) placed in Files
is used instead of the built-in copy, which is how a TU117-specific booter can be supplied.
