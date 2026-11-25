#!/bin/sh
set -e
cd "$(dirname "$0")/../../.."
T=$(mktemp -d)
sed -e "/^#include/d" drivers/net/wifi/realtek/rtw88/rtw_ra.c > $T/src.c
grep -v "^#pragma\|^#include\|^extern volatile\|^int rtw_h2c_cmd" include/nvx/drivers/net/wifi/rtw_ra.h > $T/ra_src.c
cat $T/src.c >> $T/ra_src.c
gcc -I$T -Wall -Wextra -fsanitize=address,undefined -g tools/tests/wifi/ra_main.c -o $T/ra_test
$T/ra_test
