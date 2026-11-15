# cam report (v11.02)
Commit on c-cam: uvc_webcam.c only.
## Findings
1. HIGH/high: "code 17" is xHCI cc 17 = **Parameter Error** (not Resource Error=7; Bandwidth=8, Secondary BW=35). The HS isoch EP context in uvc_cfg_ep (drivers/usb/xhci/storage_fat_and_hubs.c:237) already matches Linux xhci-mem.c for HS: Interval=bInterval-1, Max Burst=(wMPS>>11)&3, Mult=0 (non-SS), MaxESIT=MPS*(burst+1), AvgTRB=MaxESIT, CErr=0, type 5. So the bad parameter is likely NOT the EP context; suspects (out of my slice): slot context copied from output ctx (XHID_DEVCTX(cam.reg)) -- wrong reg/devctx for hub-attached device, or hub slot fields (Hub bit / MTT, storage_fat_and_hubs.c:69 sets Hub, never MTT bit 25); or cam.speed mis-decoded for a hub port. Needs a diag dump of input slot ctx + EP ctx on real HW.
2. HIGH/med: uvc_start (uvc_webcam.c ~35) could do up to 32 iterations of probe/commit control transfers + Configure Endpoint; with v11.01's xh_ctrl timeout recovery each failing iteration waits for timeouts -> multi-second/indefinite UI freeze when opening Camera (the app autostarts in camera.c:46).
3. MED/high: ausnd_write/ausnd_rec_read paths call ausnd_start on every write when start failed -> a Configure Endpoint + SET_INTERFACE per audio buffer (repeated commands every frame).
## Changes
- uvc_start: hard bounds (max 6 Configure Endpoint attempts, max 3 control failures, 4 s deadline), alt settings refused by the controller are marked bad and the next-smallest fitting alt is tried (step-up/down), cc 0 (command timeout) aborts immediately, clean teardown (drop EP) on SET_INTERFACE failure and on giving up; message names cc 17 "bad endpoint setting" and cc 8/35 bandwidth.
- UAC: ausnd_start/ausnd_rec_start back off 5 s after a failure and give up after 4 failures (reset on re-attach).
## Tests
ccheck xhci.c ok; tools/tests/uvc host test: 25 ok, all passed (not extended -- no time).
