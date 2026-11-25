#!/bin/sh
cd "$(dirname "$0")/../../.." || exit 2
T=$(mktemp -d)
python3 - drivers/bluetooth/transport/btusb.c $T/btusb_part.c <<'PY' || { rm -rf $T; exit 1; }
import sys
s=open(sys.argv[1]).read().split('\n')
a=[i for i,l in enumerate(s) if l.startswith('static int btu_mem(void)')][0]
b=[i for i,l in enumerate(s) if l.startswith('#define BTK_OBUF')][0]
c=[i for i,l in enumerate(s) if l.startswith('int btusb_evt(')][0]
keep=[l for l in s[:a] if not l.startswith('#include')]+s[c:c+6]+s[b:]
open(sys.argv[2],'w').write('\n'.join(keep)+'\n')
PY
gcc -O1 -g -Wall -Wextra -Wno-unused-function -Wno-misleading-indentation -I. -fsanitize=address,undefined -DBTUSB_PART="\"$T/btusb_part.c\"" -o $T/bulk tools/tests/bluetooth/bulk_main.c || { rm -rf $T; exit 1; }
$T/bulk; rc=$?; rm -rf $T; exit $rc
