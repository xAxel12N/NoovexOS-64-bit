#!/bin/sh
R=$(cd "$(dirname "$0")/../../.." && pwd); T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT INT TERM
sed '/^#include/d' "$R/drivers/input/evdev.c" > "$T/evdev_k.c" || exit 1
sed -e '/^#include/d' -e '/^volatile int mouse_hscroll; volatile int g_gesture;/d' "$R/drivers/input/input.c" > "$T/input_k.c" || exit 1
sed -e '/^#/d' "$R/include/nvx/drivers/input/evdev.h" > "$T/evdev_h.h"; grep "^#define EVDEV_" "$R/include/nvx/drivers/input/evdev.h" >> "$T/evdev_h.h"
gcc -O1 -g -Wall -Wno-unused-function -Wno-misleading-indentation ${SANFLAGS:-} -I"$T" -o "$T/evdev_t" "$R/tools/tests/input/evdev_main.c" || exit 1
"$T/evdev_t"
