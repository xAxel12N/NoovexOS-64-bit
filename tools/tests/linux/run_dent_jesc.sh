#!/bin/sh
set -e
D=$(dirname "$0"); R=$D/../../..; T=$(mktemp -d)
sed -n '/^static void lx_dent_old/,/^}/p' "$R/sys/linux/syscalls/fds_and_system_info.c" > "$T/lx_dent_old.inc"
sed -n '/^static char\* nvx_jesc/,/^}/p' "$R/net/http/http.c" > "$T/nvx_jesc.inc"
gcc -fsanitize=address,undefined -fno-sanitize-recover=all -I"$T" -o "$T/t" "$D/dent_jesc_t.c" && "$T/t"
rm -rf "$T"
