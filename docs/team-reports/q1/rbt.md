Task rbt (worktree /home/claude/wt/rbt, branch q-rbt): extend drivers/bluetooth/rtlbt_fw.c IC table from 2 entries to the FreeBSD
usr.sbin/bluetooth/rtlbtfw 16-entry table (BSD-2; fetch and verify), with CONFIG/MSFT flags and config-append logic, plus the 58 extra
non-0x0bda USB VID:PIDs in btusb's claim list (only if the interface is the BT class or listed). Firmware: currently embedded via
build.sh:38; for new chips load from /lib/firmware/rtl_bt/<name> (the fw coder bundles them) and keep the embedded path for 8821C/8822C
exactly as it is (owner's 0BDA:C123 must behave identically). Extend tools/tests/bluetooth (epatch tests) and run `check.py --only bt,usb`.
Write the exact list of firmware filenames you need into your report early (the fw coder reads it).
