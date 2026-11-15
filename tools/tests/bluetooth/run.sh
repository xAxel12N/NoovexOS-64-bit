#!/bin/sh
# Host test: HCI event parser + Realtek epatch loader (real rtl_bt blobs), cross-checked with epatch_ref.py
set -e
cd "$(dirname "$0")/../../.."
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. -DFWDIR='"assets/firmware/rtl_bt"' tools/tests/bluetooth/hci_main.c -o /tmp/bt_hci_t
/tmp/bt_hci_t | tee /tmp/bt_hci_t.out
python3 - <<'PY' || exit 1
import re
s=open('drivers/bluetooth/hci.c').read(); m=re.search(r'em\[8\]=\{([^}]*)\}',s); b=[int(x,16) for x in m.group(1).split(',')]
v=sum(x<<(8*i) for i,x in enumerate(b)); need={0:'Inquiry Complete',1:'Inquiry Result',6:'Remote Name Complete',33:'Inquiry Result with RSSI',46:'Extended Inquiry Result',61:'LE Meta'}
bad=[n for k,n in need.items() if not (v>>k)&1]
print('  event mask enables '+', '.join(need.values())+(' ok' if not bad else ' FAIL: '+', '.join(bad)))
raise SystemExit(1 if bad else 0)
PY
fail=0
for k in v1 v2; do
  f=assets/firmware/rtl_bt/rtl8821c_fw.bin; [ $k = v2 ] && f=assets/firmware/rtl_bt/rtl8822cu_fw.bin
  python3 tools/tests/bluetooth/epatch_ref.py $f $k | while read rom len h; do
    if [ "$len" = "-1" ]; then grep -q "^$(echo $k | tr a-z A-Z) $rom -1$" /tmp/bt_hci_t.out || { echo "MISMATCH $k rom $rom"; exit 1; }
    else c=$(sha256sum /tmp/rtlbt_${k}_$rom.bin | cut -c1-16); [ "$c" = "$h" ] || { echo "MISMATCH $k rom $rom"; exit 1; }; echo "  $k ROM $rom: $len bytes, same as the Python reference"; fi
  done || fail=1
done
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. tools/tests/bluetooth/intel_main.c -o /tmp/bt_intel_t
/tmp/bt_intel_t | tee /tmp/bt_intel_t.out | tail -2
grep -q "INTEL BT: ALL PASS" /tmp/bt_intel_t.out || fail=1
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. tools/tests/bluetooth/mtk_main.c -o /tmp/bt_mtk_t
/tmp/bt_mtk_t > /tmp/bt_mtk_t.out || { cat /tmp/bt_mtk_t.out; fail=1; }
grep -E "checks passed|MTKBT" /tmp/bt_mtk_t.out
bash tools/tests/bluetooth/bcm_run.sh > /tmp/bt_bcm_t.out 2>&1 || { tail -5 /tmp/bt_bcm_t.out; fail=1; }
[ $fail = 0 ] && grep -q "ALL PASS" /tmp/bt_hci_t.out && grep -q "MTKBT HOST TEST: ALL PASS" /tmp/bt_mtk_t.out && echo "BLUETOOTH HOST TEST: ALL PASS"
