#!/bin/sh
d=$(cd "$(dirname "$0")" && pwd); r=$(cd "$d/../../.." && pwd); o=${1:-/tmp/uac_t}
gcc -O1 -g -Wall -Wextra -Wno-misleading-indentation -fsanitize=address,undefined -fno-sanitize-recover=all -I"$d/shim" -I"$r" -o "$o" "$d/main.c" || exit 1
"$o"
