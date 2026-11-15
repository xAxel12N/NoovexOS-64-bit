Task fw (worktree /home/claude/wt/fw, branch q-fw): bundle REDISTRIBUTABLE firmware from linux-firmware for drivers we have, so they work
out of the box. Get files from the linux-firmware git (try https://gitlab.com/kernel-firmware/linux-firmware raw files or
git.kernel.org; GitHub mirrors if needed). For each file check WHENCE: licence must allow redistribution; copy the matching LICENCE.*
file and the WHENCE lines.
Wanted (check exact filenames the loaders ask for in drivers/net/wifi/iwx/ on branch p-iwx (git show p-iwx:...), drivers/bluetooth/
intelbt_fw.c, mtkbt_fw.c):
 - Intel Wi-Fi: iwlwifi-cc-a0-*.ucode (AX200), iwlwifi-QuZ-a0-hr-b0-*.ucode / Qu-*-hr (AX201), iwlwifi-ty-a0-gf-a0-*.ucode + .pnvm (AX210),
   only the ONE newest API version each that the loader accepts. LICENCE.iwlwifi_firmware.
 - Intel BT: intel/ibt-20-1-3.sfi/.ddc (AX200), ibt-19-0-4 (AX201), ibt-0041-0041.sfi/.ddc (AX210) or what intelbt_fw.c computes.
 - MediaTek BT: mediatek/BT_RAM_CODE_MT7961_1_2_hdr.bin, BT_RAM_CODE_MT7922_1_1_hdr.bin. LICENCE.mediatek (check).
 - Realtek BT: more rtl_bt files for chips the rbt coder is adding (rtl8761bu, rtl8822b, rtl8852au/bu/cu fw+config) - coordinate by reading
   /home/claude/wt/rbt when it appears; at least rtl8761bu_fw.bin and rtl8822b_fw/config.
Put them under assets/firmware/linux-firmware/<same subpath>/ plus licences, and add build.sh lines that copy them into the image as
/lib/firmware/<subpath> (find $SR / sysroot handling near build.sh:304) and licences into /usr/share/licenses/firmware/. Do NOT run build.sh;
check with `bash -n build.sh`. Keep total under ~25 MB; list sizes. Never add Broadcom .hcd (not redistributable).
Also verify the loaders really look in /lib/firmware/... on the rootfs the image provides (check the VFS path / how files are read).
