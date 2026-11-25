#!/bin/sh
R=$(cd "$(dirname "$0")/../../.." && pwd); T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT INT TERM
gcc -O1 -g -w ${SANFLAGS:-} -I"$R" -o "$T/ippusb_t" "$R/tools/tests/usb/ippusb_main.c" || exit 1
"$T/ippusb_t"
