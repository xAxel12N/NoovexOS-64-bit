#!/bin/sh
set -e
D=$(cd "$(dirname "$0")" && pwd); R=$(cd "$D/../../.." && pwd); T=${TMPDIR:-/tmp}/hidsim.$$
mkdir -p "$T"; trap 'rm -rf "$T"' EXIT
python3 "$D/hidsim_gen.py" "$R/drivers/usb/xhci/controller_init.c" "$R/drivers/usb/xhci/hid_keyboard_mouse.c" "$R/include/nvx/drivers/usb/xhci.h" "$T/hidsim_t.c" "$D/hidsim_main.c"
gcc -O1 -w -fsanitize=address,undefined -o "$T/hidsim_t" "$T/hidsim_t.c"
"$T/hidsim_t"
