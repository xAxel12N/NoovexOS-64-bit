# I3 - hardware/drivers convenience ideas (v11.04)

## Already exists (do NOT propose)
- notify()/notify_from()/toast() API: include/nvx/graphics/ui/wm.h:46-48
- USB device CONNECT notification "<Kind> connected" + name: drivers/usb/xhci/storage_fat_and_hubs.c:102 (usbdev_note)
- USB headset/mic connect + "Headset disconnected" toasts: drivers/usb/xhci/uvc_webcam.c:263,265
- Wi-Fi notifications: connected, connection lost, portal sign-in, password not accepted: net/wifi/wifi_mgr.c:204,247,278,289
- Low battery 15%/7%, 3% auto-shutdown, "Charger connected - shutdown cancelled": apps/system/powerui.c:103-112
- Battery tooltip with time left (app_tiles_and_icons.c:273), Settings Battery card (settings/pages.c:176)
- Laptop key toasts (kbd light, 80% charge limit): drivers/acpi/laptop.c
- Shell: lsusb/usbdevs (network_and_apps.c:103), usbinfo, usb (rescan), battery/bat, wifi status, lag
- Task Manager "Devices" + "Hardware" tabs (apps/system/taskmgr.c:71,230,278); Settings "Bluetooth & devices"/Devices page uses g_usbdev (settings/pages.c)
- Voice Recorder has an input level meter (apps/media/recorder.c:91); Camera app (apps/media/camera.c)
- Linux-side: /sys tree + netlink uevents for hotplug

## Top 3 NEW ideas (~10 min each)

### 1. USB DISCONNECT notification (biggest win for the owner's wireless receivers/camera/stick)
Proof missing: usbdev_gone() (storage_fat_and_hubs.c:103) just clears used and bumps g_usbdev_gen - no notify. Connect has one (line 102), unplug is silent.
Change, drivers/usb/xhci/storage_fat_and_hubs.c usbdev_gone(): collect the first removed entry's kind/name and the count; after the loop, if count>0 && usb_seen_init:
  count==1 -> notify("<kind> disconnected", name or "USB"); count>1 (hub pulled) -> notify("USB hub disconnected", "<n> devices removed").
One notify per call (hub removal clears all entries on that root port at once). Same context as the existing connect notify, so no new locking risk.
Optional extra: if the removed port was msd_port (hotplug poll line ~120), the drive notify says "USB drive removed".

### 2. Charger plugged/unplugged toast
Proof missing: pm_battery_tick (apps/system/powerui.c:104-108) only toasts on connect when a shutdown was pending; grep for "charger (connected|unplugged)|on battery power" finds nothing else.
Change, apps/system/powerui.c pm_battery_tick: static int g_bw_ac=-1; compute ac = !dis; if(g_bw_ac>=0 && ac!=g_bw_ac && !g_bw_shut_at) pm_toast(ac?"Charger connected - <p>%":"On battery - <p>% left"); g_bw_ac=ac; (first tick only records state - no boot toast). Must sit BEFORE the early `return` in the !dis branch.

### 3. Headphone jack toast
Proof missing: hda_jack (drivers/sound/hda.c:146-152) only kapi_log()s; USB headsets already toast (uvc_webcam.c:263/265), the analog jack does not.
Change, drivers/sound/hda.c hda_jack: save old=jk_hp before assignment; after kapi_log, if(old>=0){ extern void toast(const char*); toast(in?"Headphones plugged in - speakers off":"Headphones removed - sound on the speakers"); } (old==-1 skips boot-time toast). Coder: confirm hda_jack runs from the main-loop tick, not an IRQ/other CPU (drivers/acpi/laptop.c already calls toast from driver code the same way).

All three: compile-check with /home/claude/ccheck.sh <file>; #1 covered by `python3 tools/check.py --only usb`.
