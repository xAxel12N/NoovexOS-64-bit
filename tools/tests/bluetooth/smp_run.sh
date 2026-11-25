#!/bin/sh
cd "$(dirname "$0")/../../.." || exit 2
T=$(mktemp -d)
sed -n '/^void aes_cmac/,/aes128_enc(rk,X,out); }/p' net/wifi/wpa3.c > $T/cmac.c
gcc -O1 -w -I. -fsanitize=address,undefined -DCMAC_SRC="\"$T/cmac.c\"" -o $T/smp_vec tools/tests/bluetooth/smp_vec.c || { rm -rf $T; exit 1; }
$T/smp_vec; rc=$?; rm -rf $T; exit $rc
