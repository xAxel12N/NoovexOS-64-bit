#!/bin/sh
R=$(cd "$(dirname "$0")/../../.." && pwd); T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT INT TERM
python3 "$R/tools/tests/usb/hidmouse_gen.py" "$R/drivers/usb/xhci/controller_init.c" "$R/drivers/usb/xhci/hid_keyboard_mouse.c" "$R/include/nvx/drivers/usb/xhci.h" "$T/hidparse_t.c" "$R/tools/tests/usb/hidparse_main.c" || exit 1
gcc -O1 -g -w ${SANFLAGS:-} -o "$T/hidparse_t" "$T/hidparse_t.c" || exit 1
"$T/hidparse_t"
