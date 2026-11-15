#!/bin/sh
R=$(cd "$(dirname "$0")/../../.." && pwd); T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT INT TERM
python3 "$R/tools/tests/usb/hidkbd_gen.py" "$R/drivers/usb/xhci/controller_init.c" "$R/drivers/usb/xhci/hid_keyboard_mouse.c" "$R/include/nvx/drivers/usb/xhci.h" "$T/hidkbd_t.c" || exit 1
gcc -O1 -g -Wall -Wno-unused-function ${SANFLAGS:-} -I"$R/tools/tests/usb" -o "$T/hidkbd_t" "$T/hidkbd_t.c" || exit 1
"$T/hidkbd_t"
