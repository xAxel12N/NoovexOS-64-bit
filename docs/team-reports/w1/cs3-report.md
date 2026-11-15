# cs3: buffer-bug sweep (whole OS, third_party/tools excluded)

## Method
- `scopy` (lib/base/fmt.c:5) has no bound and is used 1332 times. A script (/tmp/cs3/scan.py) listed every line that declares a `char x[N<=128]` and also scopy's a non-literal into it: 141 lines. I checked each one by hand against the real maximum length of its source.
- I read by hand the parsers that take untrusted input: DHCP/DNS/TCP rx (net/stack/tcpip.c), Wi-Fi scan IEs (rtw8821ce.c:334-365), the HID report-descriptor parser and decoder (controller_init.c:160-205, hid_keyboard_mouse.c:57-92), USB FAT dir and LFN (hid_keyboard_mouse.c:289, fs/fat/vfat.c:75-92), HDA widget graph (hda.c), browser chunked/gzip/header code (browser/glue/http.c), downloads, cookie header, https_request, the docx/pptx rel targets (writer.c:215, files_classic.c:196), the .desktop parser (xapps.c), dt_path / ntfsk paths, the terminal prompt, toast/notify/win_frame, and the Linux hostname/prctl/signal-index paths.
- Result: those parsers are bounded. Every bug I found is a fixed-size stack or static buffer in UI or shell code that is too small for names or paths the OS itself allows (file names up to 63 chars, paths up to 191, user names up to 31). None is on the taskbar or Start-menu click path.

## Findings (most important first)

### 1. `chmod` writes past `b2[40]` on the stack when the file name is 27+ chars. MEDIUM, 85%
shell/commands/accounts_files_info.c:158. The line is "  " + mode_str (10) + " " + FS name (up to 63) + NUL, which needs up to 77 bytes. Scenario: in Terminal, `chmod 755 my-very-long-script-name.sh` (27 chars) overwrites stack bytes. A 63-char name overwrites 37 bytes, which corrupts the saved registers or return address of run_cmd_one, so the kernel crashes or behaves oddly. The kernel shell's if-chain is reached before /bin/chmod (run_cmd_one -> jc_builtin -> builtins).
Patch:
`char mb[12]; char b2[40],*p=b2;` → `char mb[12]; char b2[96],*p=b2;`

### 2. A shell redirect `> file` overflows `b[80]` when the target path is about 50+ chars. MEDIUM, 90%
shell/commands/prompt_and_dispatch.c:261. outfile is up to 63 chars (line 215). The message is "  appended " (11) + up to 10 digits + " bytes to " (10) + outfile + NUL, which is up to 95 bytes. Scenario: `ls > /home/harald/Documents/projects/noovex/notes/listing.txt` (more than 49 chars, multi-digit size) smashes the stack of run_cmd. That path is the main command runner for Terminal and Files.
Patch:
`char b[80],*o=b; o=scopy(o,append?"  appended ":"  wrote ");` → `char b[128],*o=b; o=scopy(o,append?"  appended ":"  wrote ");`

### 3. Restoring from Trash overflows `b[96]` when the original folder path is more than 83 chars. MEDIUM, 90%
apps/system/file_explorer/trash_and_sort.c:252 and :257. `od` is up to 190 chars (fx_info_find copies up to 190, line 206). The message is "Restored to " (12) + od. Scenario: delete a file from a deep folder (e.g. /home/harald/Documents/Projects/Noovex/website/assets/images/2026/november/), then use Restore in Files. The stack is smashed in a Files click handler. toast() truncates to 79 chars anyway, so only the buffer size needs to change.
Patch (on both lines):
`char b[96],*p=b; p=scopy(p,"Restored to "); p=scopy(p,od);` → `char b[208],*p=b; p=scopy(p,"Restored to "); p=scopy(p,od);`

### 4. Files right-click on an ELF, then Run / Run as admin, overflows `cl[220]`. LOW-MEDIUM, 85%
graphics/ui/input.c:444. The command is "sudo " (5) + p (dt_path, up to 191) + "/" + name (up to 63) + NUL, which is up to 261 bytes. Scenario: an ELF in a folder whose path is more than about 150 chars, with a long file name, then the "Run as administrator" item. The overflow is on the stack of the context-menu click handler. run_cmd then copies into work[512], which is fine.
Patch:
`char p[192],cl[220]; dt_path(FS[g_fsel].dir,p);` → `char p[192],cl[272]; dt_path(FS[g_fsel].dir,p);`

### 5. A USB headset/mic name of 45+ chars overflows static `aud_rname[56]`. LOW, 80%
drivers/usb/xhci/uvc_webcam.c:259 (declared at :244). aud_name is up to 47 chars (:249). The code writes aud_name + " microphone" (11) + NUL, which is up to 59 bytes, so up to 3 bytes of the next .bss variable get corrupted. The input is device-controlled (the iProduct string).
Patch:
`static char aud_rname[56];` → `static char aud_rname[64];`

### 6. The X app watchdog log overflows `kb[48]` when the app's base name is 25+ chars. LOW, 85%
apps/x11/xapps.c:141. g_wt is up to 31 chars (xrun_launch, :188-189). The line is g_wt + ": no window after 2 min" (23) + NUL, or ": exited, code " (15) + 10 digits, which is up to 57 bytes. Scenario: launch a Linux app with a long binary name (e.g. `gnome-system-monitor-extra` from a .desktop Exec line) that exits or never maps a window. ff_report's stack is smashed.
Patch:
`char kb[48],*kp=kb;` → `char kb[80],*kp=kb;`

### 7. The screenshot path overflows `g_ss_last[80]` for user names of 30-31 chars. LOW, 75%
apps/system/screenshot.c:93 (declared at :24). The path is g_home ("/home/" + name, up to 37) + "/Pictures/" (10) + "Screenshot-YYYY-MM-DD-HHMMSS[-N].png" (32-35) + NUL, which is up to 83 bytes. It is a global, so a neighbouring global gets corrupted. g_home itself is 96 bytes.
Patch:
`char g_ss_last[80];` → `char g_ss_last[144];`
Check that no extern declares a size. `grep -rn g_ss_last` shows only `char g_ss_last[80]` and uses.

### 8. Regedit "New key" under a 63-char key writes 1 byte past `nk[64]`. LOW, 80%
apps/system/regedit.c:90. key is up to 63 chars. The code writes key + '\\' (64 bytes), then `*p=0` lands at nk[64]. After that L=64 and `re_cp(p,re_in,0)` writes nk[64] again.
Patch:
`char nk[64]; p=nk; p=scopy(p,key);` → `char nk[72]; p=nk; p=scopy(p,key);`
reg_add_key normalises to 64 via rg_norm, so a too-long result is rejected with REG_EBADKEY instead of overflowing.

## Checked and OK (so nobody re-checks them)
- DHCP options, DNS answer walk, ARP, ICMP and TCP rx in tcpip.c: all reads are bounded by n or within the 1600-byte rx buffer.
- Wi-Fi scan IEs and SSID copies (32-char caps), EAP outer identity (n<94), WPA passphrase/SSID lengths.
- HID descriptor parser: rid<256, bit offsets fit in short, cv_cnt<12, rcount<=1024; hid_bits checks off+sz<=len*8.
- USB FAT 8.3 names (nm[16], max 13). VFAT LFN: ord<=20, n<250 + 4 < 256.
- kdown[128]: hid2scan max 0x50. Fonts: index masked to 32..126.
- Browser chunk_done/dechunk/gzip header, hdr_get, content-length clamp, download names dn[48], ck_header (tmp<=368 into ch[400]), https_request (fixed host), nvx_file_put req/body layout.
- docx tgt[96] into full[128], pptx tgt[64] into full[80], .desktop Name/Exec/Icon caps, icon path p[200], xapps paths p[160].
- dt_path is a macro using sizeof (all callers pass arrays). ntfsk nk_cat is capped at 191. Terminal prompt b[240] (31+1+31+1+170). win_frame t[64] is capped at 62. toast is capped at 79. notify title/body/app are capped.
- Hostname setters (31 cap and validated), prctl comm (15), signal arrays [65] with sig<=64 checks, jobs cmd[64] into m[96]/m[120].
- Properties dialog nm[80]/b[44] (pr_w is a constant 420), settings user line b[40] (31+8+1=40 exactly), camera USB list b[96], USB headset toast b[96] / klog b[120].
