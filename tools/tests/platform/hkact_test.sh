#!/bin/sh
d=$(dirname "$0"); o=${TMPDIR:-/tmp}/hkact_t
gcc -O1 -Wall -I"$d/../../.." -Wno-unused-function -Wno-address -Wno-misleading-indentation ${SANFLAGS:--fsanitize=address,undefined} -o "$o" "$d/hkact_main.c" && "$o"
