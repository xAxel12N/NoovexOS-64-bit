# s4 - drivers/ (not usb): periodic or IRQ code that can hang

Scope covered: everything the main loop (core/init/main.c:297-364) and desk_nested_step/ui_pump/busy_pump
(graphics/ui/desktop/present_and_pump.c:118-152) call outside drivers/usb: tp_poll, etp_poll, syn_poll, vm_mouse_poll,
acpi_poll (EC + AML), snd_poll (HDA/AC97), ctemp_poll, cpupower_tick, nic_watch, wifi_mgr_tick (rtw88), nvd_poll/nvgsp_tick.
IRQ side: outside drivers/usb the only driver IRQ code is the PS/2 byte handlers (syn_irq_byte, etp_irq_byte), which only
fill a ring. No AHCI/NVMe/HDA/rtw88 interrupt handlers exist (all polled). So a non-USB driver hang has to come from the main loop.
Every register or fence wait I found is limited by hr_ns()/a counter: NVIDIA 2D fence 200 ms, compute/3D fence 500 ms,
nv_poll, kc_idle, GSP msg queue, HDA CORB/RIRB/stream reset (1000x10us), EC aec_wait (20000 inb), ACPI global lock 50 ms,
rtw h2c 300x100us, PS/2 200000. No wait loops on g_ticks with interrupts off. No unbounded poll found.

## Ranked findings

### 1. NVIDIA "automatic re-check" fires on the first 2 s with no input -> i.e. right after a drag ends. Confidence 25% (only if the machine has a Turing NVIDIA GPU)
- drivers/video/nvidia/gsp/tick_on_off.c:7-9 (nvgsp_tick, called every main-loop pass via nvd_poll, core/init/main.c:348).
  Once g_auto_at is set (boot.c:121 nv_auto_need, after any 2D/compute/3D fence timeout or a screen-copy readback
  mismatch, sc_off boot.c:270), it waits until `g_ticks-g_last_input_t>=200` (2 s with no mouse/key) and then, inside the
  main loop, runs nvgsp_all_off() + nvgsp_on() -> twod() + nvd_selftest() (GPU work + GSP RPCs) + nvd_request() which on
  the next pass runs nvd_desktop()/gfx_start(GFX_GPU) (re-allocates all window layers in VRAM).
  Why it matches: while dragging, input arrives every frame so the re-check keeps being pushed back 1 s; the moment the
  mouse goes still it runs. "When I didn't drag the window anymore it froze" = first idle window. Also, the drag itself
  is the heaviest GPU-drawing load, the most likely moment for a 2D/compute fence to time out and arm g_auto_at.
- drivers/video/nvidia/nvdraw.c:99-108 (nd_auto): 3 s after the desktop first appears, on Turing (dev 0x1E00-0x1FFF,
  e.g. GTX 1650) it runs the whole `nvgsp_cmd("all")` (GSP-RM boot, many seconds) synchronously in the main loop, and before
  that nd_usb_flag()/nd_usb_set() do msd_autoinit_safe() + ufat_write_file("NVSTART.TXT") to the USB stick (USB bulk
  I/O from the main loop). Matches "freeze N s after desktop/app opens" if the owner was quick. If this hung, next boot
  logs "nvgsp: the NVIDIA start-up did not finish last time - skipped" (ask the owner / check NVSTART.TXT=1 on the stick).
- Also at drag release the window-content cache turns ON (present_and_pump.c desk_win_exposed); under GFX_GPU that
  allocates VRAM layers (nd_alloc_vram, g_gfx_nomem path nvdraw.c:116) - first GPU allocation after the drag.
- Fix (exact): v11.03 make both automatic paths opt-in:
  boot.c:120 `static u32 g_auto_tries=1;` (disables the idle re-check; user can still run `nvgsp on`), and
  nvdraw.c:86 `int g_nvd_auto=0` (no automatic GSP start; Gears/`nvgsp all` still works). Lower-risk alternative:
  in tick_on_off.c:7 also require `drag<0` and `g_ticks-g_last_input_t>=1000`, and in nd_auto skip unless 30 s idle.
  Verification ask: owner's log lines starting "nvgsp:" and whether the machine is the GTX 1650 desktop.

### 2. Wi-Fi Leisure Power Save turns on in the background after DHCP. Confidence 8%
- net/wifi/wifi_mgr.c:265-266 wm_ps_tick: 10 s after connect and 5 s after the last TX it sends H2C LPS-on (wm_lps:260).
  After that the main loop keeps doing MMIO on the card every 200 ms (wm_card_dead rtw_r32(0x0000) wifi_mgr.c:288,
  net_idle_poll RX ring reads) while the firmware may have the chip in 32k/PS state. On some RTL8821CE + host bridges a
  read while the chip is asleep stalls the PCIe completion (CPU stops inside one MMIO read, nothing in software can time
  it out). Timing is "N s after the last network traffic", unrelated to the drag - only coincidence would link it.
  wm_pre_tx leaves LPS only before TX, not before RX/register polling.
- Fix: wifi_mgr.c default `g_wm_ps=0` for v11.03 (Linux rtw88 also disables deep PS on 8821CE by default), or call
  wm_lps(0) before net_idle_poll/wm_card_dead.

### 3. ACPI EC/AML polling every 20 ms (also inside desk_nested_step). Confidence 4%
- drivers/acpi/acpi_dev.c:247-254 acpi_poll -> aec_query (up to 8 queries, each aec_wait up to 20000 inb, about 20-40 ms)
  + _Qxx AML eval with 500 ms hard deadline (g_aml_hard_deadline). Worst case is a stall of under 1 s, then back-off of 5-30 s
  (aec_wait:37-41). Can cause stutter, not a permanent freeze. Desktop PC: usually no EC at all (g_aec.ok=0).
  Note: aec_query ignores aec_lock failure (global lock not taken) - harmless on a desktop.

### 4. Not a risk (checked)
- ui_pump (called inside rtw8821ce/rtw8822c/wpa2/tcpip/ahci waits) only polls the touchpad/xHCI pointer and redraws the cursor;
  it does not run process_input, so a drag release cannot be handled inside a driver wait. desk_nested_step (which does run
  process_input + compose_present) is called only from apps/internet/browser/glue/startup.c:41.
- HDA hda_poll (drivers/sound/hda.c:154): jack check every 300 ms, codec cmd waits limited; backs off to 30 s if the codec is dead.
- Recorder/camera capture path is USB (uvc_poll, ausnd_rec_read via cap_pump core/sound.c:38) - outside my scope, but note
  recorder_tick->cap_pump calls up to 64 rec_read per pass from the main loop (USB team should check rec_read's wait limit).
- All the time limits above depend on hr_ns() (TSC). If the TSC calibration were wrong, the limits would stretch, but they still end.

Bottom line: no unbounded poll in non-USB drivers. The only non-USB code that is *triggered by input going idle* is the
NVIDIA auto re-check (#1); it matters only on a Turing NVIDIA machine with GPU drawing on.
