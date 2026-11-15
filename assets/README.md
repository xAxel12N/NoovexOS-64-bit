# assets

Data built into the kernel image: fonts, wallpapers, the DOOM and LibreQuake data, installer images, the NVIDIA firmware (`nvidia/`) and the RTL8821CE firmware (`firmware/`).

The firmware is redistributable but not covered by the NoovexOS licence. Its licence texts must ship with every image: `nvidia/LICENCE.nvidia` and `firmware/LICENCE.rtlwifi_firmware.txt` (build.sh copies both to `/usr/share/licenses/firmware/` in the system files).
