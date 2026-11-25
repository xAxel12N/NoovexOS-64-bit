#!/bin/sh
# Host test: HCI event parser + Realtek epatch loader (real rtl_bt blobs), cross-checked with epatch_ref.py
set -e
cd "$(dirname "$0")/../../.."
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. -DFWDIR='"assets/firmware/rtl_bt"' tools/tests/bluetooth/hci_main.c -o /tmp/bt_hci_t
/tmp/bt_hci_t | tee /tmp/bt_hci_t.out
python3 - <<'PY' || exit 1
import re
s=open('net/bluetooth/hci.c').read(); m=re.search(r'em\[8\]=\{([^}]*)\}',s); b=[int(x,16) for x in m.group(1).split(',')]
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
gcc -O1 -g -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined -I. tools/tests/bluetooth/intel_flow_main.c -o /tmp/bt_iflow_t
/tmp/bt_iflow_t > /tmp/bt_iflow_t.out || { grep FAIL /tmp/bt_iflow_t.out; fail=1; }
grep -E "checks passed" /tmp/bt_iflow_t.out | sed 's/^/  Intel setup flow: /'
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. tools/tests/bluetooth/mtk_main.c -o /tmp/bt_mtk_t
/tmp/bt_mtk_t > /tmp/bt_mtk_t.out || { cat /tmp/bt_mtk_t.out; fail=1; }
grep -E "checks passed|MTKBT" /tmp/bt_mtk_t.out
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. tools/tests/bluetooth/l2cap_main.c -o /tmp/bt_l2cap_t
/tmp/bt_l2cap_t | grep -v "log:" > /tmp/bt_l2cap_t.out || { cat /tmp/bt_l2cap_t.out; fail=1; }
tail -1 /tmp/bt_l2cap_t.out; grep -q "L2CAP HOST TEST: ALL PASS" /tmp/bt_l2cap_t.out || fail=1
bash tools/tests/bluetooth/bcm_run.sh > /tmp/bt_bcm_t.out 2>&1 || { tail -5 /tmp/bt_bcm_t.out; fail=1; }
bash tools/tests/bluetooth/smp_run.sh > /tmp/bt_smp_t.out 2>&1 || { tail -5 /tmp/bt_smp_t.out; fail=1; }
grep -E "LE pairing checks" /tmp/bt_smp_t.out
bash tools/tests/bluetooth/bulk_run.sh > /tmp/bt_bulk_t.out 2>&1 || { grep -E "FAIL|error" /tmp/bt_bulk_t.out | head -5; fail=1; }
grep -E "checks passed" /tmp/bt_bulk_t.out | sed 's/^/  bulk recovery: /'
bash tools/tests/bluetooth/hidrep.sh > /tmp/bt_hidrep_t.out 2>&1 || { tail -5 /tmp/bt_hidrep_t.out; fail=1; }
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. tools/tests/bluetooth/sbc_main.c -o /tmp/bt_sbc_t -ldl -lm
/tmp/bt_sbc_t > /tmp/bt_sbc_t.out 2>&1 || { cat /tmp/bt_sbc_t.out; fail=1; }
tail -1 /tmp/bt_sbc_t.out; grep -q "SBC HOST TEST: ALL PASS" /tmp/bt_sbc_t.out || fail=1
if command -v ffmpeg >/dev/null 2>&1 && ffmpeg -v error -y -f sbc -i /tmp/bt_sbc_t.sbc -f s16le -ac 2 -ar 48000 /tmp/bt_sbc_t.dec 2>/dev/null; then
python3 - <<'PY' || fail=1
import struct
a=open('/tmp/bt_sbc_t.raw','rb').read(); b=open('/tmp/bt_sbc_t.dec','rb').read(); n=min(len(a),len(b))//2
x=struct.unpack('<%dh'%n,a[:2*n]); y=struct.unpack('<%dh'%n,b[:2*n]); best=-99
import math
for d in range(0,600,2):
    s=sum(v*v for v in x[4000:40000]); e=sum((x[i]-y[i+d])**2 for i in range(4000,40000)) or 1; best=max(best,10*math.log10(s/e))
print('  SBC stream decoded by ffmpeg: SNR %.1f dB'%best+(' ok' if best>20 else ' FAIL')); raise SystemExit(0 if best>20 else 1)
PY
fi
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. tools/tests/bluetooth/hfp_main.c net/bluetooth/sco.c net/bluetooth/sbc/sbc_enc.c net/bluetooth/sbc/sbc_dec.c -o /tmp/bt_hfp_t
/tmp/bt_hfp_t | grep -v "log:" > /tmp/bt_hfp_t.out 2>&1 || { cat /tmp/bt_hfp_t.out; fail=1; }
tail -1 /tmp/bt_hfp_t.out; grep -q "HFP HOST TEST: ALL PASS" /tmp/bt_hfp_t.out || fail=1
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. tools/tests/bluetooth/hfpin_main.c net/bluetooth/sco.c net/bluetooth/sbc/sbc_enc.c net/bluetooth/sbc/sbc_dec.c -o /tmp/bt_hfpin_t
/tmp/bt_hfpin_t | grep -v "log:" > /tmp/bt_hfpin_t.out 2>&1 || { cat /tmp/bt_hfpin_t.out; fail=1; }
tail -1 /tmp/bt_hfpin_t.out; grep -q "HFP INCOMING HOST TEST: ALL PASS" /tmp/bt_hfpin_t.out || fail=1
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. tools/tests/bluetooth/a2dp_main.c -o /tmp/bt_a2dp_t
/tmp/bt_a2dp_t | grep -v "log:" > /tmp/bt_a2dp_t.out 2>&1 || { cat /tmp/bt_a2dp_t.out; fail=1; }
tail -1 /tmp/bt_a2dp_t.out; grep -q "A2DP HOST TEST: ALL PASS" /tmp/bt_a2dp_t.out || fail=1
gcc -O1 -g -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined -I. tools/tests/bluetooth/btui_main.c -o /tmp/bt_ui_t
/tmp/bt_ui_t > /tmp/bt_ui_t.out 2>&1 || { cat /tmp/bt_ui_t.out; fail=1; }
tail -1 /tmp/bt_ui_t.out; grep -q "BT UI: ALL PASS" /tmp/bt_ui_t.out || fail=1
grep -q 'if(id>=2800&&id<2900){ st_bt_heavy(id); return; }' apps/system/settings/pages.c && echo "  Settings routes buttons 2800-2899 (pairing Yes/No, audio) to st_bt_heavy ok" || { echo "  Settings does not route 2800-2899 FAIL"; fail=1; }
sh tools/tests/bluetooth/sim_run.sh > /tmp/bt_sim_t.out 2>&1 || { tail -8 /tmp/bt_sim_t.out; fail=1; }
grep -E "checks passed" /tmp/bt_sim_t.out
[ $fail = 0 ] && grep -q "BT SIM: ALL PASS" /tmp/bt_sim_t.out && grep -q "ALL PASS" /tmp/bt_hci_t.out && grep -q "MTKBT HOST TEST: ALL PASS" /tmp/bt_mtk_t.out && echo "BLUETOOTH HOST TEST: ALL PASS"
