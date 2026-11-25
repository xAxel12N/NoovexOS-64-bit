#!/bin/sh
set -e
cd "$(dirname "$0")/../../.."
O=/tmp/bt_sim; mkdir -p $O; F="-O1 -g -w -fsanitize=address,undefined -I. -include tools/tests/bluetooth/sim_shim.h"
U="net/bluetooth/hci.c net/bluetooth/conn.c net/bluetooth/l2cap.c net/bluetooth/bt_mgr.c net/bluetooth/profiles/hid_host.c net/bluetooth/profiles/hid_report.c"
S=""; [ -f net/bluetooth/sec.c ] && { U="$U net/bluetooth/sec.c"; S="-DSIM_SEC"; }
objs=""; for u in $U; do o=$O/$(echo $u | tr / _).o; gcc $F -c $u -o $o; objs="$objs $o"; done
K=""; [ -n "$S" ] && { gcc $F -c tools/tests/bluetooth/sim_keys.c -o $O/sim_keys.o; K=$O/sim_keys.o; }
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. $S tools/tests/bluetooth/sim_main.c $objs $K -o $O/sim
$O/sim "$@"
