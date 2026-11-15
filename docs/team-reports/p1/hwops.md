Task hwops: split net/wifi (wifi_mgr, wifi_netdev, wpa2/3, eap) from the RTL8821CE driver behind a small ops table so a second chip
driver (Intel iwx, being written in parallel in worktree iwx) can plug in. Define include/ (or net/wifi/) wifi_hw.h with a struct like
wifi_hw_ops { name; init; scan_start; scan_results/feed; join(bssid,ch,...); leave; tx(frame,len); set_key(idx,type,key,len,pairwise);
get_rssi; poll; } plus wifi_hw_register(const struct wifi_hw_ops*, void*ctx) and a receive entry point wifi_hw_rx(frame,len,rssi).
Convert rtw8821ce.c to register through it with ZERO behaviour change for the owner (it works today). Keep it minimal; no
refactors beyond what's needed. Write the header contract clearly in your report so the iwx coder can follow it; the lead merges both.
Run `python3 tools/check.py --only wifi` before committing.
