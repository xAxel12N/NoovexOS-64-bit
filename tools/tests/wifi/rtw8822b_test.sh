#!/bin/sh
# Host test for the RTL8822BE chip file: regenerates the expected table walk from the Linux source and runs the test.
set -e
R=$(cd "$(dirname "$0")/../../.." && pwd)
LINUX=${LINUX:-/tmp/ref/linux}
T=${TMPDIR:-/tmp}/rtw8822b_test; mkdir -p $T
python3 -I $R/tools/tests/wifi/rtw8822b_gen.py $LINUX/drivers/net/wireless/realtek/rtw88/rtw8822b_table.c $T/rtw8822b_expect.h
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -I$R/tools/tests/wifi/rtw8822b_stub -I$R -I$T $R/tools/tests/wifi/rtw8822b_main.c -o $T/rtw8822b_test
$T/rtw8822b_test
