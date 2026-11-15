#!/bin/sh
# Host test: Broadcom .hcd parser + patchram command sequence against a simulated controller
set -e
cd "$(dirname "$0")/../../.."
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. tools/tests/bluetooth/bcm_main.c -o /tmp/bt_bcm_t
/tmp/bt_bcm_t
