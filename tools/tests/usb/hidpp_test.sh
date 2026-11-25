#!/bin/sh
R=$(cd "$(dirname "$0")/../../.." && pwd); T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT INT TERM
python3 "$R/tools/tests/usb/hidpp_gen.py" "$R/drivers/usb/xhci/logitech_hidpp.c" "$T/hidpp_t.c" "$R/tools/tests/usb/hidpp_main.c" || exit 1
gcc -O1 -g -Wall -Wno-unused-function -Wno-misleading-indentation ${SANFLAGS:-} -o "$T/hidpp_t" "$T/hidpp_t.c" || exit 1
"$T/hidpp_t"
