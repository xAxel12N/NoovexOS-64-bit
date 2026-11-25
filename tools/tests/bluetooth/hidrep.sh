#!/bin/sh
set -e
cd "$(dirname "$0")/../../.."
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. tools/tests/bluetooth/hidrep_main.c -o /tmp/bt_hidrep_t
/tmp/bt_hidrep_t
