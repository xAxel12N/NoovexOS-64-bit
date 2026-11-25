#!/bin/sh
set -e
D=$(dirname "$0"); R=$D/../../..; T=$(mktemp -d)
sed -n '/^static int dns_build_query/,/^}/p' "$R/net/stack/tcpip.c" > "$T/dns_build_query.inc"
gcc -fsanitize=address,undefined -I"$T" -o "$T/t" "$D/dns_query_t.c" && "$T/t"
rm -rf "$T"
