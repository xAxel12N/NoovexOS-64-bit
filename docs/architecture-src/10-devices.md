# Part 10: Input, USB, ACPI, sound and graphics drivers

This part goes through the device drivers that are not storage, network or Wi-Fi. For each one it covers the hardware interface, how the driver starts, how data moves, where waits are bounded, which shell command shows its state, and what it does not do. All paths are relative to the repository root. Every file here is a kernel unit with a header under `include/nvx/`, except the two sound back ends. `build.sh` compiles `drivers/sound/ac97.c` and `drivers/sound/hda.c` as separate objects (`MODS=...`) against `include/kapi.h`.

## 10.1 Where the drivers start and how they are polled

All bring-up runs from `kmain` (`core/init/main.c`) in a fixed order once the PIC and PIT are up:

```c
if(inb(0x64)==0xFF){ klog("boot: no PS/2 controller (USB keyboard/mouse only)"); }
else { for(int i=0;i<64&&(inb(0x64)&1);i++) inb(0x60); mouse_init(); klog("boot: ps2 mouse ok"); }
xhci_mouse_autoinit(); klog("boot: xhci probe done");
tp_init(); klog("boot: touchpad probe done");
g_acpi_pwrbtn_cb=acpi_power_button; acpi_aml_init(); klog("boot: ACPI AML done");
battery_init();
```

`power_init()` runs much earlier, right after `klog_init()`, so the S5/reset data is ready before anything else. `get_brand()` (cpuinfo) runs just before `pci_scan()`. Sound is not started at boot. `snd_init()` probes lazily the first time something needs audio.

The main loop is the only place that polls these devices. Only PS/2 is interrupt driven (IRQ1 and IRQ12):

| Main-loop call | Rate | Driver work |
|---|---|---|
| `tp_poll(); etp_poll();` | every iteration | I2C-HID/ELAN-I2C touchpad read; drain the ELAN PS/2 packet ring |
| `acpi_poll(); snd_poll();` | every 2 ticks (20 ms) | EC queries, power button, ACPI notifications, battery; audio ring bookkeeping |
| `xhci_mouse_poll()` | every iteration while `xhid_n` is non-zero | USB HID event rings and key repeat |
| `usb_hotplug_poll()` | gated to 80 ticks (0.8 s) inside the function | root-port and hub-port connect/disconnect |
| `battery_tick()` | each loop in `ST_DESKTOP` | top-bar battery percentage (ACPI or legacy EC) |

When there is no USB HID device and no runnable JS job, the loop halts (`cpu_halt`) until the next interrupt. That is why USB input forces a busy loop: no xHCI interrupts are set up (the comment in `xhci.c` says "Poll-based (no MSI)").

## 10.2 Shared input state and the key queue

Every input driver writes into one set of globals declared in `net/http/http.c` (the include order puts them there):

```c
static volatile int mouse_dx=0, mouse_dy=0;        /* accumulated deltas     */
static volatile int mouse_btn=0, mouse_btn_prev=0; /* left button            */
static volatile int mouse_rbtn=0, mouse_rbtn_prev=0;
static volatile int mouse_scroll=0; static volatile int mouse_mid=0;
static int g_mbytes=3;                             /* 3, or 4 = IntelliMouse */
static volatile int mouse_evt=0;
#define KBUFN 64
static void kpush(char c){ int n=(kh+1)%KBUFN; if(n!=kt){kbuf[kh]=c;kh=n;} }
static volatile unsigned char kdown[128];
static int kpop(void){ if(kh==kt)return -1; char c=kbuf[kt]; kt=(kt+1)%KBUFN; return c; }
```

- `kbuf` is a 64-entry single-producer/single-consumer ring. It holds 63 characters, and when it is full new keys are dropped (`kpush` checks `n!=kt`). `kpop()` returns -1 when the ring is empty.
- Arrow keys are encoded as control codes: left 17, right 18, up 19, down 20. Both the PS/2 path (`kbd_feed`) and the USB path (`usb_key_code`) use them. Escape is 27.
- `kdown[]` is a held-key table indexed by PS/2 set-1 scancode. USB keyboards fill it through `hid2scan()`, which maps letters, digits, arrows, Enter, Esc, Backspace, Tab and Space, plus modifiers at 0x1D/0x2A/0x38/0x5B. That lets shortcuts such as Win+R (`graphics/ui/input.c`: `kdown[0x5B]` and `kdown[0x13]`) work with either keyboard.

`drivers/input/input.c` (7 lines) adds the helpers that the touchpad drivers use. Each one wraps its update in `cpu_irq_save` so it cannot race the IRQ12 handler:

| Helper | Effect |
|---|---|
| `in_rel(dx,dy)` | `mouse_dx+=dx; mouse_dy-=dy; mouse_evt=1` |
| `in_buttons(l,r,m)` | sets the three button globals |
| `in_scroll(v,h)` | `mouse_scroll+=v; mouse_hscroll+=h` |
| `in_gesture(g)` | `g_gesture=g` (consumed once by `graphics/ui/input.c`) |

The consumer is `process_input()` in `graphics/ui/input.c`. It disables interrupts, snapshots and clears the deltas, and feeds `ux_mouse_push()` (the `/dev/input/mouse` device, VFS dev 10). It applies optional acceleration (`g_mouse_accel`) and moves the cursor. Gestures map to desktop actions there: swipe up opens the launcher, swipe down closes the launcher and notifications, and swipe left or right switches workspace. The pinch gestures are produced but not acted on in `graphics/ui/input.c`.

## 10.3 PS/2 keyboard and mouse (`drivers/input/ps2/ps2.c`, `ps2_mouse.c`)

Hardware. The i8042 data port is 0x60 and the status/command port is 0x64. `pic_remap()` unmasks IRQ0-2 on the master (`outb(0x21,0xF8)`) and IRQ12 on the slave (`outb(0xA1,0xEF)`). `isr_body` routes both IRQ1 and IRQ12 to one function:

```c
static void ps2_byte(void){
    u8 st=inb(0x64); if(!(st&1)) return;
    u8 v=inb(0x60);
    if(st&0x20) mouse_feed(v); else kbd_feed(v);   /* status bit 5 = byte came from the aux port */
}
```

Keyboard. `kbd_feed` decodes scancode set 1 with two 128-entry tables (`kmap_lo`, `kmap_up`). The state flags are `kshift` (0x2A/0x36), `kalt` (0x38), `kcaps` (toggled by 0x3A) and `kext` (the 0xE0 prefix). Extended make codes 0x4B/0x4D/0x48/0x50 become the arrow codes. Releases (bit 7) only clear `kdown[]`. Caps Lock inverts the case of letters only. No keyboard initialisation is sent: the driver relies on the firmware's translated set 1. There are no LEDs, no keymaps other than US, and no function keys (F1-F12 map to 0).

Mouse init (`mouse_init`, `ps2_mouse.c`). Every controller wait is bounded:

```c
static int pw_write(void){ int t=200000; while((inb(0x64)&2)&&--t)io_wait(); return t; }
static int pw_read (void){ int t=200000; while(!(inb(0x64)&1)&&--t)io_wait(); return t; }
static int mcmd(u8 c){ if(!pw_write())return 0; outb(0x64,0xD4); if(!pw_write())return 0; outb(0x60,c);
    if(!pw_read())return 0; inb(0x60); return 1; }      /* consumes the ACK byte */
```

The init sequence:

1. Enable the aux port (0xA8).
2. Read the config byte (0x20), set bit 1 (IRQ12) and clear bit 5 (aux clock disable), and write it back (0x60).
3. Try `etp_detect()` (section 10.4). If it finds a v3/v4 Elantech pad, set `g_mbytes=6`, enable reporting and stop.
4. Otherwise send defaults (0xF6), then the IntelliMouse "magic knock" (set rate 200, 100, 80), then Get ID (0xF2). An ID of 3 switches to 4-byte packets (`g_mbytes=4`).
5. Enable reporting (0xF4) and set `g_mouse_ok=1`.

Packet path. `mouse_feed` resyncs on bit 3 of the first byte. It drops packets with the X/Y overflow bits (0xC0) set, sign-extends the deltas from flag bits 4 and 5, and adds the wheel byte (`int8_t mb[3]`) to `mouse_scroll`. When an ELAN pad is active (`g_etp_hw>=3`), every byte goes to `etp_irq_byte` instead.

Inspect: `mousetest` prints movement, buttons and wheel for up to 3000 ticks (30 s); a key press stops it early. `usbinfo` ends with `PS/2: controller present` or `no controller`.

Limits: only the first PS/2 port gets a keyboard; there is no hot-plug and no 5-button mice.

## 10.4 Elan PS/2 touchpad (`drivers/input/ps2/elan_ps2.c`)

This is a native Elantech v3/v4 decoder. It feeds the shared gesture engine (section 10.5.4) instead of plain mouse emulation.

Detection (`etp_detect`), all through `mcmd` on the aux port:

| Step | PS/2 sequence | Check |
|---|---|---|
| Magic | `F5`, `E6 E6 E6`, `E9` (3 bytes) | `3C 03 C8` or `3C 03 00` |
| Firmware | "sliced" command: `E6`, then four `E8 <2 bits>` pairs, then `E9` | `etp_sig_ok()` rejects rate-like answers |
| Version | `hw=1` if fw<0x020030 or fw==0x020600; IC 2/4 to v2; 5 to v3; at least 6 to v4 | only `hw>=3` is accepted |
| Capabilities | `F8 02 E9` to `g_etp_cap` | |
| Range | `F8 00 E9` to `maxx`, `maxy` | both at least 16 |
| Resolution (v4) | `F8 04 E9` to units/mm via `etp_res_upmm` | default 31 |
| Absolute mode | `etp_write_reg`: reg 0x07=0x01 (v4) or reg 0x10=0x0B (v3) | |

Data path. In IRQ context, `etp_irq_byte` collects 6 bytes and classifies them with `etp_kind()`: v4 head/motion/status packets, v3 packets, the v3 debounce packet `C4 FF FF 02 FF FF`, and trackpoint packets (`(p[3]&0x0F)==6`). An invalid packet makes it shift by one byte and try again (`g_etp_bad++`). Valid packets go into a 64x6-byte ring (`g_etp_ring`); when the ring is full it drops the packet and counts `g_etp_drop`. In the main loop, `etp_poll()` drains up to 64 packets. `etp_packet` updates five finger slots (absolute position for v4 head packets, +/-deltax(1 or 5) for motion packets) and `etp_emit` calls `tpe_frame()`. On a v3 pad a 3-finger report has only one position, so it is copied into three contacts to keep swipes working. Trackpoint packets go straight to `in_rel`/`in_buttons`.

Inspect: `tpinfo` prints `ELAN PS/2 touchpad: Elantech vN firmware ... packets / bad / frames`. For a v1/v2 pad it prints `(old version, used as a basic mouse)`.

## 10.5 I2C HID touchpads

The stack has five layers, each in its own file:

```
touchpad.c   tp_init / tp_poll / tpinfo   - find controller + device via ACPI, pick protocol
i2c_hid.c    ih_*                         - HID-over-I2C protocol (descriptor, reset, features, input)
elan_i2c.c   el_*                         - native ELAN I2C protocol (alternative to HID)
tp_hid.c     tp_parse / tp_bits           - HID report-descriptor parser for PTP + mouse collections
tp_gesture.c tpe_frame                    - pointer, tap, scroll, pinch, swipe engine
dw_i2c.c     dw_*                         - Synopsys DesignWare I2C master
```

### 10.5.1 DesignWare I2C controller (`drivers/i2c/dw_i2c.c`)

This is MMIO only. `dw_present(base)` requires a page-aligned base below 4 GB and `IC_COMP_TYPE` (0xFC) equal to `0x44570140`.

| Reg | Off | Use |
|---|---|---|
| `IC_CON` | 0x00 | `1 \| speed<<1 \| 1<<5 \| 1<<6`: master, std/fast speed, restart enable, slave disable |
| `IC_TAR` | 0x04 | 7/10-bit target address |
| `IC_DATA_CMD` | 0x10 | bit 8 read, bit 9 STOP, bit 10 RESTART |
| `SS/FS_*CNT` | 0x14-0x20 | written only if firmware left them 0 (600/705, 90/195) |
| `IC_RAW_INTR_STAT` | 0x34 | bit 6 = TX_ABRT |
| `IC_ENABLE` / `IC_ENABLE_STATUS` | 0x6C / 0x9C | toggled by `dw_enable()`, polled at most 100000 `io_wait` |
| `IC_STATUS` | 0x70 | bit 5 master activity, polled at most 100000 at the end |
| `IC_TXFLR` / `IC_RXFLR` | 0x74 / 0x78 | FIFO fill |
| `IC_TX_ABRT_SOURCE` | 0x80 | saved in `last_abort` (shown by `tpinfo`) |
| `IC_COMP_PARAM_1` | 0xF4 | FIFO depths (falls back to 8) |

`dw_xfer(c,addr,w,wn,r,rn)` does one combined write-then-read. It disables the controller, sets the target and re-enables it. It then keeps the TX FIFO full with write bytes followed by read commands (RESTART on the first read, STOP on the last byte), while keeping at most `rxd-1` reads outstanding. The loop is bounded at 400000 iterations. Return codes: `-1` bad args or not initialised, `-2` enable timeout, `-3` abort (NACK, arbitration loss), `-4` timeout. No interrupts and no DMA are used.

### 10.5.2 Finding the touchpad (`drivers/input/touchpad/touchpad.c`)

`tp_init_ex(scan_all)` uses the raw AML pre-scanner (`aml_scan.c`, section 10.7.2), not the interpreter, because it runs before `acpi_aml_init()`:

1. `aml_load_from_acpi(); aml_scan_tables();` collect devices with `_HID`, `_ADR`, `I2cSerialBus`, `GpioInt` and `Memory32Fixed` resources. Devices with `PNP0C50`/`ACPI0C50` are marked `is_hidi2c`.
2. Controller list (`tp_list_ctrls`, at most 12):
   - ACPI devices `AMDI0010`/`AMDI0510` with a memory resource.
   - On AMD CPUs (CPUID vendor check `tp_is_amd`), fixed MMIO 0xFEDC2000-0xFEDC6000 in 4 KiB steps.
   - Intel PCI functions on bus 0 with class 0x0C80. `tp_pci_dw` enables memory decode, moves the function to D0 through its PM capability (10 ms wait), and if `COMP_TYPE` does not match, writes 7 to BAR+0x204 and checks again.
3. For each candidate device (HID-over-I2C or one of 52 `ELAN0000`-`ELAN1000` IDs), resolve its `I2cSerialBus` controller path to a base address (`tp_ctrl_base`). If the resource has no address, try `TP_ADDRS = {0x2C,0x15,0x2A,0x10,0x20,0x24,0x38,0x40,0x14,0x5D}`. If it has no speed, use 400 kHz. A device that has an ELAN ID and is not PNP0C50 tries the native ELAN protocol first, then HID.
4. HID descriptor registers are tried in the order `TP_DREGS = {0x20,0x01,0x00,0x10,0x30}`.
5. If the device's `GpioInt` controller is `AMDI0030`, the interrupt line is read directly: `gpio_base + pin*4`, bit 16, honouring active-low.

`scan_all=1` additionally brute-forces every controller x address x descriptor register. Its only caller is `tpinfo_run(1)`, reached with `tpinfo scan` (the shell checks the first letter of the argument after `tpinfo`).

Once a descriptor register returns a valid HID descriptor, no other register is tried at that address. `ih_get_desc` decodes into a local copy and writes `t->d` only after the descriptor is valid, so a later failed probe cannot overwrite a good vendor and product. The `_CRS` address that is put in front of the static resource list is removed from the rest of the list (paths compared without `_` and `\`), so it is tried once. A device whose `_HID` starts with `ELAN` is a candidate even without PNP0C50; it is tried with HID first and the ELAN protocol second, like any HID pad with vendor 0x04F3.

### 10.5.3 HID over I2C (`drivers/input/touchpad/i2c_hid.c`) and report parsing (`tp_hid.c`)

`ih_setup` walks through numbered stages. The stage number is stored in `t->stage` and `tpinfo` prints it when a probe fails:

| Stage | Action |
|---|---|
| 1 | `SET_POWER ON` (opcode 0x08 to `wCommandRegister`), 2 ms |
| 2 | `RESET` (0x01). Poll up to 200x5 ms for the zero-length reset report |
| 3 | Read `wReportDescLength` bytes from `wReportDescRegister` into a 4 KiB buffer, then read it again into a second buffer and compare. Up to 4 attempts; if no two reads match, or the read fails, the bus is set up again at 100 kHz and read again |
| 4 | `tp_parse()` the report descriptor. If the reads never matched, or no touchpad or mouse layout was found, the descriptor is read once more at 100 kHz and parsed again |
| 5 | If it is a PTP: `SET_REPORT(Feature)` Input Mode (usage 0x0D:0x52) = 3, then Surface/Button switches (0x57/0x58) = 1 |
| 6 | Ready. `mode=1` is precision touchpad; `mode=2` is the mouse collection fallback |

The 30-byte HID descriptor is accepted only if `wHIDDescLength==30`, `bcdVersion==0x0100`, the report descriptor is 1..4096 bytes and `wMaxInputLength` is 3..512.

Hybrid reports (one finger per report) are assembled into frames: a report whose Contact Count is above 0 starts a frame that needs ceil(count / fingers per report) reports, and reports with count 0 continue it. Contacts with Confidence 0 are dropped as palms. On Intel Sunrise Point/Kaby Lake (`INT344B`), Apollo Lake (`INT3452`) and Gemini Lake (`INT3453`) the `GpioInt` pin is mapped straight to its pad (`igp_setup` in `tp_igpio.c`), so reports are read only while the pad asserts the line; other Intel controllers learn the pad, and without a pin up to 4 reports are read per 10 ms tick.

`tp_parse` is a general HID item parser with a 4-deep Push/Pop stack. It does not give up on items it cannot use: a Main item with a Report Size outside 1..32 or a Report Count above 4096 is skipped (its bits still count toward the report length), a truncated item ends the parse at that byte, and collections deeper than 16 are counted but not tracked. The item count, the number of skipped items, the byte of the first skipped item and the byte where the data was cut off are kept for `tpinfo` and `tpdump`. It records bit offsets per report ID into a `tplay`:
- under a Digitizer/Touch Pad application (0x0D:0x05), each Finger logical collection (0x0D:0x22) becomes a finger slot (max `TPF_MAX` 10) with Tip, Confidence, Contact ID, X and Y fields;
- Contact Count (0x54), Scan Time (0x56) and Button 1-3;
- the feature fields Input Mode (0x52), Contact Count Max (0x55), Button Type (0x59) and Surface/Button switch;
- a Generic Desktop Mouse application (0x01:0x02) for the fallback.

The physical size comes from Unit/Unit Exponent through `tp_unit_um` (cm or inch). A PTP is rejected if no finger has Tip, X and Y, or if the logical range is empty.

`tp_poll` reads one input report (`dw_xfer` with no write phase, `wMaxInputLength` bytes):
- With a GPIO, it reads while the line is asserted, up to 4 reports per call.
- Without one, it reads at most once per 10 ms tick and drops a report identical to the previous one (`dup++`).

`ih_process` checks the 2-byte length and the report ID, and gathers contacts whose Tip (and Confidence, if present) are set. Hybrid-mode reports that are split across frames (Contact Count > fingers per report) are buffered in `pend[]` until complete.

### 10.5.4 Gesture engine (`drivers/input/touchpad/tp_gesture.c`)

`tpe_frame(E, contacts, n, button, ms, &out)` is shared by I2C-HID, ELAN-I2C and ELAN-PS/2. All its thresholds are in micrometres, converted using `upmm_x/upmm_y` (units per mm):

| Behaviour | Rule in code |
|---|---|
| Pointer (1 finger, or any count while the clickpad is pressed) | gain 8.0x up to 200 um/frame, rising linearly to 30.0x at 2000 um, with sub-pixel remainder kept in `fx/fy` |
| Tap | touch lifted within 220 ms, total movement at most 2500 um, no button used: 1 finger to left, 2 to right, at least 3 to middle. The release is sent next frame (`pulse`) |
| Clickpad right button | button pressed with at least 2 fingers, or one finger at x>55 %, y>80 % |
| Two-finger scroll | one wheel step per 2000 um; natural scrolling on by default (`E->natural=1`) |
| Pinch | finger distance changes by >8000 um and by more than half the accumulated movement to `GES_ZOOM_IN/OUT` |
| 3+ finger swipe | 15000 um along the dominant axis to one `GES_SWIPE_*` per touch |

### 10.5.5 ELAN native I2C (`drivers/input/touchpad/elan_i2c.c`)

`el_setup` writes `0x0100` (reset) to register 0x0005 and waits 100 ms. It then reads the 30-byte descriptor (all-0xFF means no device) and the 158-byte report descriptor. Next it reads pattern (0x0100), product (0x0101), firmware (0x0102), IC type (0x0103 or 0x0110 depending on pattern), SM version (0x0104; bit 4 means clickpad), max X/Y (0x0106/0x0107) and resolution (0x0108). It selects absolute mode (0x0300 = 1), wakes the device (0x0005 = 0x0800) and clears the power-off bit in 0x0307. Reports are 34 bytes (pattern at most 1, ID 0x5D) or 39 bytes (high-precision, ID 0x60), with five 5-byte finger records gated by `touch` bits 3..7. Report IDs 0x5E/0x5F carry trackpoint data.

Inspect: `tpinfo` lists the ACPI touchpad candidates (path, HID, I2C address/controller/speed, interrupt pin), the DesignWare controller bases, the ELAN PS/2 status, and the active protocol with vendor/product, finger range, size in mm, read/frame/tap counters, last gesture and the fingers currently down. Boot log lines start with `touchpad:`.

## 10.6 xHCI USB (`drivers/usb/xhci.c`)

### 10.6.1 Controllers and memory

`xhci_init()` scans every bus/device/function for class 0x0C0330, up to `XHC_MAX` 4 controllers. Per-controller state is swapped into the driver's globals with `xhc_select(i)` (`xhc_save`/`xhc_load`), so all the code below works on "the current controller". All rings and contexts sit at fixed identity-mapped addresses below 16 MiB (the controller DMAs to them, and phys==virt):

| Region | Address | Size |
|---|---|---|
| Controller 0 base (`g_xmem`) | 0x00990000 | DCBAA +0, command ring +0x1000, event ring +0x2000, ERST +0x3000, scratchpad array +0x4000, buffers +0x5000 |
| Controllers 1-3 | `g_xhc_mem[k-1]` (static, 0x26000 each, page aligned) | same layout |
| HID/hub device region r (0..9) | `0x00A00000 + r*0x8000` | input ctx, device ctx, EP0 ring, interrupt-IN ring, data buffer (4 KiB each) |
| Mass storage | 0x00A50000-0x00A66000 | contexts, EP0/IN/OUT rings, CBW/CSW, 64 KiB sector buffer |

Every ring is 256 TRBs with a Link TRB (type 6, Toggle Cycle) in slot 255. Scratchpads: up to 32 buffers fit in the fixed area. Larger requests use `dma_alloc_pages(spb+1)`, and if that fails the controller is not used.

### 10.6.2 Controller start (`xhci_init_one`)

1. Read BAR0. A 64-bit BAR whose high half is non-zero sets `xhci_hi4g` and the controller is skipped (`usbinfo`: "registers above 4 GB, cannot be used yet"). Enable MEM + bus master.
2. On Intel, copy `USB3PRM` to `USB3_PSSEN` (0xDC to 0xD8) and `XUSB2PRM` to `XUSB2PR` (0xD4 to 0xD0) to route the shared ports to xHCI.
3. BIOS handoff: find extended capability ID 1 and set the OS-owned byte. Wait up to 400000 `io_wait` for BIOS-owned (bit 16) to clear; if it does not, clear it by force (klog "firmware did not release the controller, taking it"). Then clear the SMI enables in USBLEGCTLSTS.
4. Wait for CNR to clear, clear Run/Stop and wait for HCHalted, then HCRESET and wait (200000/400000-step bounds).
5. `CONFIG.MaxSlotsEn = HCSPARAMS1.MaxSlots`, set DCBAAP, `CRCR = cmdring | RCS`. Interrupter 0 gets ERSTSZ=1, ERDP and ERSTBA.
6. Run, power every port (PORTSC.PP, bit 9), wait ~40000 delays, then record CCS and speed for each port.

### 10.6.3 Commands, events and control transfers

`xh_cmd_s` enqueues a command TRB, rings doorbell 0 and waits for a Command Completion event (type 33). Success is completion code 1. `xh_event_t` polls the event ring for at most 400000 iterations, advances ERDP with the EHB bit, and parks transfer events that belong to other endpoints in `xh_q[32]`, so a control transfer during enumeration does not lose HID reports. `xh_ctrl` builds Setup/Data/Status TRBs on the current EP0 ring. A STALL (cc 6) is recovered with Reset Endpoint (14) and Set TR Dequeue Pointer (16). Short packet (cc 13) counts as success.

### 10.6.4 Enumeration (`xhci_enum_loc`)

Enable Slot (9) to Address Device (11) with EP0 max packet size 8/64/512 by speed. For full-speed devices, `xh_fix_mps0` reads 8 bytes of the device descriptor and runs Evaluate Context (13) when bMaxPacketSize0 is 16/32/64. Then GET_DESCRIPTOR device (18 bytes), config (9 bytes, then the full descriptor, capped at 256 bytes). After that:

- Interface class 9 to hub (`xhub_attach`, section 10.6.6).
- Interface class 3 to HID. `hid_pick` keeps interfaces that have an interrupt-IN endpoint (max 6). Then for each interface:
  - subclass 1 / protocol 1 (boot keyboard): use the boot protocol, 8-byte reports;
  - anything else: GET_DESCRIPTOR(Report, 0x22) and `hid_parse` it into up to 4 `hidlay_t` layouts. If that succeeds the interface uses the report protocol, with report IDs if present. If it fails, a boot mouse (1/2) falls back to boot protocol.
  - SET_PROTOCOL (0x0B) is sent to boot-subclass interfaces, and SET_IDLE (0x0A) to keyboards.
- Each kept interface gets its own region and interrupt-IN ring. All of them share one slot, and a single Configure Endpoint (12) programs every endpoint (type 7, interval from `xh_interval`). The first Normal TRB is armed with IOC (`xhid_arm`).

That per-interface layout is what makes multi-function devices work, such as a combined keyboard + mouse, or a wireless receiver exposing both on one device. There is no vendor-specific receiver code: such devices work if their interfaces are standard HID with report descriptors `hid_parse` understands (keyboard bitmap or array, mouse buttons/X/Y/wheel).

Failures stop at a named step stored in `xhci_failstep`, for example `"Address Device"` or `"no keyboard or mouse interface"`, together with `xhci_failcc`.

### 10.6.5 HID data path

`xhci_mouse_poll()` to for each controller, `xhci_poll_one()`. It first replays parked events, then handles up to 128 new events. For each transfer event, `hid_xfer` finds the endpoint, copies up to 16 bytes into `xhid_last` (shown by `usbinfo`) and decodes:

- boot mouse: `btn, int8 dx, int8 dy[, int8 wheel]`;
- boot keyboard, or a report-protocol keyboard turned into an 8-byte boot report: `kbd_report` diffs against the previous report, pushes ASCII through `usb_key_code` and updates `kdown[]`. Caps Lock (usage 0x39) toggles the shared `kcaps`. Reports with ErrorRollOver (`d[2]==1`) are ignored;
- report protocol: `hid_report_decode` uses the parsed bit offsets.

Each HID endpoint has 16 transfers in flight (`XHID_INFLIGHT`). `xhid_arm` gives each Normal TRB its own 64-byte slot in the region's data buffer, rotating through `xhid_bs[]`, so reports that arrive while the desktop is redrawing are not overwritten. The transfer event carries the TRB address; `hid_xfer` reads the buffer pointer back from that TRB and decodes the report from its own slot. Parked events (`xh_q`) now store the TRB address too. After decoding, one new TRB is queued, which keeps 16 outstanding. Before v9.05 only one TRB was queued, so a 1000 Hz wireless mouse lost most of its reports and the backlog in the dongle made the pointer lag.

Key repeat is done in software (`usb_kbd_repeat`): the first repeat comes after 50 ticks (500 ms), then every 3 ticks (30 ms).

### 10.6.6 Hubs

`XHUB_MAX` 6 hubs, tiers up to depth 5, and up to 15 ports per hub. In order:

1. Read the hub descriptor (0x29, or 0x2A for SuperSpeed).
2. Mark the slot context as a hub (Hub bit, port count, TT think time).
3. For SuperSpeed hubs send SET_HUB_DEPTH (request 12).
4. Power every port (feature 8) and wait `max(100, bPwrOn2PwrGood*2)` ms.
5. Enumerate connected ports.

`xhub_port_reset` sets PORT_RESET (feature 4) and polls up to 100x5 ms, then clears C_PORT_RESET (20) and C_PORT_CONNECTION (16). Child devices get a route string (`route | port<<(4*depth)`). A low- or full-speed device behind a high-speed hub also gets TT slot/port. A port behind a hub tries mass storage first, then HID. Removal (`xhub_remove_below`) disables every slot whose route lies under the detached port.

### 10.6.7 Mass storage and hot-plug

Mass storage uses Bulk-Only Transport. The 31-byte CBW has signature `USBC` (0x43425355) and the 13-byte CSW has `USBS`. Commands: TEST UNIT READY (up to 8 tries with 40000-delay waits), READ CAPACITY(10), READ(10) and WRITE(10) on 512-byte blocks. Only one stick is handled at a time (global `msd_ok`). On top of it, `ufat_*` implements FAT16/FAT32 for the root directory only: at most 32 entries, 8.3 names, no long file names. The partition is taken from MBR entry 1 if its type is 0x01/04/06/0B/0C/0E, otherwise it is a superfloppy. Cluster allocation scans at most 65536 clusters. A write reallocates the whole file. The FAT cache is one sector, written to all FAT copies.

`usb_hotplug_poll` (every 0.8 s) compares each root port's CCS with `usb_seen[]`. On a new device it tries mass storage first, auto-mounts it and sets `g_usb_arrived`; the desktop then opens Files and shows a notification. Otherwise it enumerates HID. On removal it tears down that port's devices. Hub ports are polled by `xhub_poll_ports`.

Shell: `usb`/`xhci` (rescan everything, then `usbinfo`); `usbinfo` (per controller: PCI location, ready/failed, scratchpad count; per port: speed and what was set up; per HID endpoint: keyboard/mouse, boot or report protocol, report count and last bytes; hubs with their connected ports; last failure step); `usbdisk`/`usbls`, `usbcat <F>`, `usbwrite <F> <text>`, `usbrm <F>`; `mousetest`.

Limits: no interrupts or MSI; no isochronous or other device classes; no USB storage beyond one stick and one FAT root; registers above 4 GB are unsupported; MSD transfers are limited to the 64 KiB buffer.

## 10.7 ACPI and power

### 10.7.1 Static tables and power states (`core/power.c`)

`acpi_find_rsdp()` tries, in order:
- the bootloader handoff (signature `RSDP` at 0x6000+128, pointer at 0x6000+120);
- the EBDA (first 1 KiB);
- 0xE0000-0xFFFFF.

The RSDP must pass its 20-byte checksum. `acpi_tbl_ok` rejects tables above 4 GB, shorter than 36 bytes, longer than 4 MiB, or with a bad checksum. `power_init` reads the FADT (SMI_CMD, ACPI_ENABLE, PM1a/b EVT/CNT including X_ GAS fallbacks, the reset register, and the hardware-reduced sleep control register) and finds `\_S5`/`\_S1` by byte-pattern search for `NameOp "_S5_" PackageOp` in the DSDT and up to 8 SSDTs. This does not need the interpreter.

| Function | Sequence |
|---|---|
| `power_off()` | flush gdisk; unmount NTFS and VFAT; HW-reduced: write `SLP_TYP<<2 \| SLP_EN(0x20)` to SLEEP_CONTROL. Otherwise `acpi_enable_mode()`, then `PM1a_CNT = SLP_TYP<<10 \| 1<<13` (and PM1b). Fallbacks `outw(0x604,0x2000)`, `outw(0xB004,0x2000)`, `outw(0x4004,0x3400)`, then the "It is now safe to turn off your computer." screen and `cpu_stop()` |
| `power_reboot()` | FADT reset register (I/O or memory) to 0xCF9 (0x02 then 0x06) to 8042 `0xFE` to triple fault |
| `power_sleep()` | S1 only (not on HW-reduced): clear WAK_STS, enable PWRBTN_EN, write SLP_TYP\|SLP_EN, spin at most 200,000,000 `cpu_pause` for WAK_STS. Without S1: black screen until a key is pressed |
| `acpi_enable_mode()` | `outb(SMI_CMD, ACPI_ENABLE)`, wait for SCI_EN at most 3,000,000 `io_wait` or 1 s |

Shell: `shutdown`/`poweroff`/`halt`, `reboot`/`restart`, `suspend`.

### 10.7.2 AML pre-scanner (`drivers/acpi/aml_scan.c`)

This is a non-executing walker for the touchpad code. It copies pointers to the DSDT and all SSDTs (max 33), finds every Scope/Device (`0x10`, `0x5B 0x82`) up to 768 nodes, and builds full paths. For each device (max 160) it scans raw bytes for:
- `_HID` (string or EISA ID, decoded by `aml_eisa`) and `_ADR`;
- the `PNP0C50`/`ACPI0C50` HID-over-I2C IDs;
- `I2cSerialBus` (0x8E, type 1): address, speed and controller path;
- `GpioInt` (0x8C): pin, polarity and controller path;
- `Memory32Fixed` (0x86).

Child devices are skipped (`aml_in_child`). Because it never runs methods, a `_CRS` that is built at runtime (`Method(_CRS)` returning a computed buffer) is still found only if the resource bytes appear literally in the device body.

### 10.7.3 AML interpreter (`drivers/acpi/aml.c`)

Namespace lookups (`anode_child`) check every node pointer before using it: the address must be at least 1 MiB, below 4 GiB and 8-byte aligned. A bad link ends the walk with "not found" instead of a page fault.

This is a full tree-walking interpreter. Values are `aobj`s (Integer, String, Buffer, Package, Reference). Namespace nodes are `anode`s covering Scope, Device, Name, Method, OperationRegion, Field, IndexField, BankField, BufferField, Mutex, Event, Processor, PowerResource, ThermalZone, Alias and native methods. Integer width follows the DSDT revision (`g_aml_rev`, 32-bit when <2). The predefined namespace contains `\_SB`, `\_GPE`, `\_PR`, `\_TZ`, `\_SI`, `_GL`, `_OS = "Microsoft Windows NT"`, `_REV = 2`, and a native `_OSI` that answers true for the Windows version strings.

Every execution is bounded:

| Guard | Value |
|---|---|
| Terms per `aml_eval` / per table load | 2,000,000 / 4,000,000 (`g_aml_budget`) |
| Wall time per evaluation | 1000 ms outside a batch, `g_aml_time_ms` in a batch, checked every 256 terms |
| Hard batch deadline | 15 s for `acpi_aml_init`, 500 ms for each `acpi_poll` |
| `Sleep()` | taken from a shared pool (`g_aml_sleep_pool`: 5000 ms at init, 300 ms per poll) |
| `Stall()` | capped at 100 us per call, charged to the same pool |
| Call depth / expression nesting | 32 / 192 |
| Buffer size / field size | 0x40000 bytes / 2^18 bits |
| Bad terms per table | the loader skips a failed package and gives up after 64 |

Method-local named objects are tracked (at most 64 per frame) and unlinked on return. `Notify` pushes into a 32-entry ring that `acpi_drain_notify` reads. A Field with LockRule takes the Global Lock through `aml_gl_hook`.

### 10.7.4 Region access, EC, battery, backlight (`drivers/acpi/acpi_dev.c`)

`acpi_aml_init` installs the hooks in `aml.c`:

| Space | Handler (`acpi_rd`/`acpi_wr`) |
|---|---|
| 0 SystemMemory | 0x1000 at most addr < 4 GB. Writes go only to RAM the firmware map marks usable for it (`mm_first_bad`), or, without a map, never below 256 MiB or into the heap. Blocked writes are counted in `g_acpi_memblock` |
| 1 SystemIO | `in/out` b/w/l, 64-bit as two dwords |
| 2 PCI_Config | 0xCF8/0xCFC; bus/dev/fn from `_BBN`/`_ADR` up the tree, following bridge secondary bus numbers |
| 3 EmbeddedControl | `aec_read`/`aec_write` |
| 5 CMOS | 0x70/0x71 (low 128 bytes), 0x72/0x73 (high) |

The Global Lock is the FACS dword at +16, taken with a compare-and-swap. Waits are capped at 50 ms. Releasing a lock that had the pending bit set writes GBL_RLS (PM1a_CNT bit 2).

Start-up order in `acpi_aml_init`:
1. Load copies of the DSDT and up to 16 SSDTs.
2. Enable ACPI mode and clear PWRBTN_STS.
3. Run `_REG` for spaces 0/1/2.
4. Set up the EC and run `_REG(3,1)`.
5. Run `_INI` on every device whose `_STA` says present, recursing into children when present or functional.
6. Find `PNP0C0A` batteries (max 2) and the `ACPI0003` adapter.
7. Read the battery, set up the backlight, drain notifications.

Embedded controller. Ports come from the ECDT, or from a `PNP0C09` device's `_CRS` (default 0x62/0x66), plus `_GPE` and `_GLK`. Commands: 0x80 read, 0x81 write, 0x84 query. Each IBF/OBF wait is at most 20000 `io_wait`. After 8 consecutive timeouts a circuit breaker disables the EC for 5 s (`trips`). `acpi_poll` runs up to 8 queries per call while SCI_EVT (status bit 5) is set and calls `_Qxx`.

Power button. `acpi_poll` reads PM1a_STS bit 8 (PWRBTN_STS), clears it, debounces for 100 ticks and calls `acpi_power_button()` to `power_off()`. This works only in polled, non-HW-reduced mode. No SCI interrupt handler is used.

Battery. It reads `_BIX` (at least 20 elements) or `_BIF` (at least 13) for design/full capacity, model, serial, type and OEM, and `_BST` for state, rate, remaining and voltage. Percentage = sumremaining/sumfull, rounded. Minutes to empty or full come from the rate (discarded above 99 h). It is refreshed every 500 ticks, and also when a battery or AC device gets a notification (0x81 forces a full `_BIX` re-read). `graphics/ui/desktop.c` prefers this data (`bat_via_ec=2`). With no ACPI battery it falls back to an older fixed-port EC reader on 0x62/0x66 that guesses the percentage from offsets {0x2A,0x32,0x42,0x90,0xB0,0x14,0x24} and the charging state from offset 0x01 bit 0.

Backlight (`bl_*`).
- `bl_find` looks for a device with both `_BCM` and `_BCL`. Its levels are the de-duplicated `_BCL` entries from index 2 on, in the range 0-100.
- `bl_intel_probe` checks for an Intel display device at 00:02.0. BAR0 is mapped even above 4 GB: `acpi_hi_map` builds 2 MiB pages (flags 0x9B) for addresses below 512 GiB. It then works out which PWM register pair is live (offsets 0xC8250/0xC8254/0xC8258 or 0x48250/0x48254 to `pwm_kind` 1-3).
- `bl_set` calls `_BCM` with the nearest level and also writes the PWM duty.
- `_DOS(4)` is called on the output's parent. Notifications 0x86/0x87 step the brightness and show a toast.
- The minimum level is the smallest non-zero `_BCL` level, or 5 % for PWM only.

Shell.
- `acpi`: tables, objects, KB of AML, set-up time, ACPI mode, AML errors, EC ports/GPE/events/timeouts, batteries/AC, backlight, notifications and blocked RAM writes.
- `acpi ls [path]`, `acpi eval <path> [int args...]`, `acpi ec` (dumps 256 EC bytes).
- `battery`/`bat`: ACPI details. With no ACPI battery, or as `battery e...`, it runs the legacy EC offset scanner.
- `brightness`/`backlight [up|down|N]`.

A second `acpi` branch further down `commands.c` (S5/S1/reset summary) cannot be reached because the earlier `seq(cmd,"acpi")` matches first.

## 10.8 RTC (`drivers/rtc/cmos.c`)

```c
static u8 cmos(u8 r){ outb(0x70,r); return inb(0x71); }
for(int _g=0;_g<100000&&(cmos(0x0A)&0x80)&&cmos(0x0A)!=0xFF;_g++){}   /* bounded UIP wait */
```

`read_clock` reads seconds, minutes, hours, day, month and year (registers 0, 2, 4, 7, 8, 9). It converts from BCD unless register B bit 2 is set, and converts 12-hour time unless bit 1 is set. Limits: the year is always `2000+y` (the century register is ignored), the time is not read twice to check consistency, there are no writes and no alarms. Commands: `time`, `date`. `uptime` uses `g_ticks/100`, not the RTC.

## 10.9 Sound (`core/sound.c`, `drivers/sound/*`)

### 10.9.1 Framework

Back ends fill a `struct nvx_snd` (`include/drivers.h`): `name, rate, start, free_bytes, write, poll, stop, volume`. `snd_init()` runs once: `hda_probe()` first, then `ac97_probe()`. It records state 1 (up) or 2 (none) and applies `g_snd_vol` (80).

The output format is fixed at 48 kHz, 16-bit, stereo, little-endian. Both back ends use the same ring (`drivers/sound/ring.h`) over a DMA buffer of 32x4096 bytes:

```c
static inline void sring_hw(struct sring* r,u32 pos){        /* pos = hardware play position */
    u32 delta=(pos+r->size-r->last)%r->size;
    for(u32 i=0;i<delta;i++)r->buf[(r->rd+i)%r->size]=0;      /* zero what was played: underrun = silence */
    r->rd+=delta; r->last=pos; if(r->wr<r->rd)r->wr=r->rd; }
static inline int sring_free(const struct sring* r){ u64 used=r->wr-r->rd; u32 guard=4096;
    if(used+guard>=r->size)return 0; return (int)(r->size-guard-used); }
```

The DMA engine loops over the whole buffer forever. The driver never takes an interrupt; it learns the position by polling (`snd_poll` every 20 ms, and also inside every `write`).

`/dev/dsp` (also `/dev/audio`, VFS dev 12): `snd_dev_write` starts the stream and writes whole frames (`len&~3`). If the ring is full it retries every 1 ms for up to 2000 tries. A JS-runtime process gets `JS_BLOCK` instead and is rescheduled. `sys/linux/linux.c` answers two OSS ioctls for the same device: 0xC0045002 (`SNDCTL_DSP_SPEED`, returns the rate) and 0x8010500C (`SNDCTL_DSP_GETOSPACE`, 32 fragments of 4096).

Shell: `audio`/`sound` (back end and HDA codec/paths), `audio vol <0-100>`, `audio tone [50-8000 Hz]` (one second of a +/-6000 triangle wave from `snd_tone`).

### 10.9.2 AC'97 (`drivers/sound/ac97.c`)

The device is PCI class 04/01 with I/O BAR0 = NAM (mixer) and BAR1 = NABM (bus master). Probe sequence:
1. Cold reset through Global Control (NABM+0x2C = 2).
2. Wait for codec-ready (NABM+0x30 bit 8), at most 2000x100 us.
3. Mixer reset (NAM+0).
4. If the codec has VRA (NAM+0x28 bit 0), enable it and set the front DAC to 48000. The rate read back is kept; below 8000 it is replaced by 48000.

Volume uses 5-bit attenuation on Master (0x02) and Headphone (0x04), with PCM-out (0x18) at 0x0808, or mute bit 0x8000 at 0 %. PCM-out registers: BDBAR 0x10, CIV 0x14, LVI 0x15, SR 0x16, PICB 0x18, CR 0x1B. The position is `CIV*4096 + (4096 - PICB*2)`. Each poll moves LVI to `CIV-1` so the 32-entry BDL never ends, and clears the status bits (and restarts the engine on DCH). The PCM buffer must lie below 0xFFFF0000.

### 10.9.3 Intel HD Audio (`drivers/sound/hda.c`)

The device is PCI class 04/03 with a 32-bit MMIO BAR (BARs above 4 GB are refused: "hda: BAR above 4 GB, not mapped").

1. Controller: reset with GCTL.CRST (0x08), with every wait at most 1000x10 us. Pick the first codec from STATESTS (0x0E). The first output stream descriptor index is ISS (from GCAP).
2. CORB/RIRB: use the largest supported sizes (2/16/256), reset the read pointers, then run both. RIRBCTL is set to `0x03` (DMA run plus the response-interrupt enable) while the controller's global interrupt enable stays off: some controllers, QEMU's `intel-hda` among them, only reset their response counter when the driver acknowledges RIRBSTS, which requires the interrupt bit, and before v9.11 they answered just the first verb. `verb()` writes the CORB and polls RIRBWP for up to 20000x10 us (200 ms), then clears RIRBSTS. When no output path is found, the log line lists every widget with its type and connection count.
3. Graph walk (`scan`): find the Audio Function Group (node type 1) and power it to D0 (0x705). For every widget (max 128 NIDs), read the capabilities (param 9) and the connection list (param 0x0E plus verb 0xF02, at most 16 entries), and power it up. Then make three passes over pin widgets that can output (PINCAP bit 4), skip pins whose port connectivity is 1 ("no connection"), and prefer default device speaker (1), then headphone (2), then line-out (0). `dfs()` searches from the pin through mixers (type 2) and selectors (type 3) to an output converter (type 0), depth at most 8. At most 4 paths are kept.
4. Route: unmute output amps (gain = offset step), set selector indices (0x701), unmute mixer input amps, and on each DAC set format 0x0011 and stream tag 1 / channel 0 (0x706 = 0x10). Each pin gets PIN_WIDGET_CONTROL 0x40, or 0xC0 for headphones (0x707), and EAPD (0x70C = 2) if the pin supports it (PINCAP bit 16).
5. Stream: SDnCTL reset/unreset, a 32-entry BDL without IOC, CBL = 128 KiB, LVI = 31, FMT 0x0011, stream tag 1 (SDnCTL byte 2 = 0x10), clear status, RUN. The position comes from SDnLPIB (+0x04).

`hda_volume` scales each path's output-amp gain from 0 to the offset step, and mutes at 0 %.

Host-side tests for both probes, including codec timeout and muted-codec cases, are in `tools/tests/drivers/drvsim.c`.

Sound limits: playback only, no capture; one stream; a fixed format; no jack detection or switching (all found paths are routed at the same time); no HDMI audio codecs beyond what the path search happens to find; the first matching PCI device only.

## 10.10 Video

### 10.10.1 Boot framebuffer (`drivers/video/fb/framebuffer.c`)

There is no display-mode driver. The kernel draws into the linear framebuffer that the bootloader handed over at 0x5000:

| Handoff | Offset | Meaning |
|---|---|---|
| `FBB` | 0x5000 (u64) | framebuffer address |
| `PITCHB` | 0x5008 (u32) | bytes per line |
| `W`, `H` | 0x500C, 0x500E (u16) | resolution, clamped to `MAXWxMAXH` = 1920x1080 |
| `BPP` | 0x5020 (u8) | 24 or 32 (`BYPP` 3 or 4) |
| RGB flag | 0x5021 (u8, valid if 0x502C == `'UEFI'`) | 1 = RGBX byte order to `g_fb_rgb` swaps R/B on blit |

All drawing goes into `SCENE[MAXW*MAXH]` (32-bit xRGB). `BG` caches the wallpaper and icons. Primitives include `fill_rect`, anti-aliased `fill_round` and `fill_circle` (coverage from `isqrt32`), `shadow_soft`, the 8x14 bitmap font (`dchar/dstr`) and proportional anti-aliased text from `fontaa` (`pdstr`). `blit_all()` copies `SCENE` to the hardware buffer row by row, honouring pitch, 24/32 bpp and the RGB swap. If `g_panel_oled` is set, the whole image shifts by 1 px through 4 positions every 3000 ticks (30 s) to protect OLED panels.

### 10.10.2 SIMD blitting (`arch/x86_64/cpu/simd.c`)

`arch_fpu_init` enables SSE (CR0.MP, clears EM, CR4.OSFXSR/OSXMMEXCPT) and sets `g_ssse3` from CPUID.1:ECX bit 9. The kernel itself is compiled with `-mno-sse`, so these inline-asm routines are the only code that touches XMM registers:

| Routine | Work |
|---|---|
| `sse_fill32(d,c,n)` | broadcast with `pshufd`, 64 bytes per iteration |
| `sse_copy32(d,s,n)` | 4x`movdqu` per 16 pixels |
| `sse_pack24(d,s,n)` | 32 to 24 bpp with one `pshufb` per 4 pixels (needs SSSE3), scalar tail |

`g_accel2d` (default 1, toggled in Settings > "2D acceleration") chooses between these and plain loops.

### 10.10.3 Effects (`graphics/theme/effects.c`)

`glass_panel` and `glass_liquid` do in-place frosted-glass effects on `SCENE`: two or three passes of a horizontal running-sum box blur of radius `blurR` (one row buffer `g_glasstmp[MAXW]`), a tint blend, top highlight gradients, and 1 px rim lines that follow the rounded-corner mask (`cut()`). `soft_ring` draws a blended annulus. Everything runs on the CPU.

### 10.10.4 Write-combining and Intel graphics (`drivers/video/intel/igpu.c`)

`igpu_early()` runs right after the framebuffer is known. `pat_cpu_init()` programs IA32_PAT (MSR 0x277) as `0x0007040600070106`: entry 1 (PWT only) becomes write-combining, the others keep their defaults (nothing else in the kernel uses PWT alone). Every CPU runs it, the application processors in `ap_entry_c`. `wc_map()` then sets PWT and clears PCD and the PAT bit on the 2 MB (or 4 KB) page-table entries that cover the framebuffer, flushes the caches and reloads CR3. Firmware MTRRs normally make the framebuffer uncached; MTRR UC combined with PAT WC gives WC, so screen writes are combined into bursts. It is only done when the framebuffer is at least 2 MB, so the 2 MB pages stay inside the display BAR. `gpuinfo` shows "write-combining" when it was applied.

`igpu_init()` (before the touchpad probe) looks at PCI 00:02.0. For an Intel display controller it reads the device ID (generation and name: Sandy Bridge to Meteor Lake), BAR0 (registers, GGTT at +8 MB on gen 8+), BAR2 (aperture) and the stolen-memory size from GGC (config 0x50). It reads `PIPESRC` (0x6001C) for the current mode, `TRANS_DDI_FUNC_CTL_EDP` (0x6F400) and the eDP or pipe A transcoder timings (0x6F000/0x6F00C or 0x60000/0x6000C) for the panel's native size, the scaler (`PS_CTRL_1_A` 0x68180, gen 9+) or panel fitter (0x68080), and the primary plane (`PLANE_CTL` 0x70180, stride 0x70188, surface 0x7019C).

Tear-free drawing (gen 8 to 12): if the plane is on, linear and 32-bit and the framebuffer is the aperture view of the scanned-out surface, a second buffer of the same size is placed in the GGTT after the first (256 KB aligned, inside the first 256 MB). Its GGTT entries point to pages from `pframe_alloc()` and copy the flag bits of the visible surface's entry; the GGTT TLB is flushed through `GFX_FLSH_CNTL` (0x101008). A test pattern written through the aperture must read back, the current screen is copied in, and the plane must report the new surface in `PLANE_SURFLIVE` (0x701AC) within 100 ms; otherwise the original surface is restored. While on, `blit_all()` waits for the previous flip to finish (up to 40 ms), draws into the hidden buffer and writes its offset to `PLANE_SURF`, which the hardware switches at the next vertical blank. `blit_region()` writes to both buffers. Three late flips turn it off. `gpu flip off`, `gpu flip on` and Ctrl+Alt+F switch it by hand. QEMU has no Intel graphics, so only the checks and the fallback were tested there.

`gpuinfo` prints the GPU, panel and current mode (native or scaled), the plane, the framebuffer (size, address, caching, boot mode), the time of a full-screen copy (average of 10 `blit_all()`), the tear-free state and the blitter state.

2D blitter (gen 9, `igpu_blt_init()`, run after tear-free drawing has started). Each stage sets a step number that `gpuinfo` reports with the reason if it stops:

| Step | Action |
|---|---|
| 1 | Checks: Intel gen 9, tear-free drawing on |
| 2 | Forcewake for the render, blitter and media domains (request 0xA278/0xA188/0xA270, acknowledge 0x0D84/0x130044/0x0D88); held while the blitter is used, released on failure |
| 3 | On Apollo Lake and Gemini Lake (no shared last-level cache) the private PAT (0x40E0/0x40E4) is set like Linux does for these chips: entry 0 and 4-7 snooped, 1-3 not. The old values are shown |
| 4 | GGTT objects after the second screen buffer, each also mapped at the same address in the per-context page table: the page directory, a test buffer (16 pages), the new GPU-shared scene buffer (`MAXW`x`MAXH` pixels from the kernel heap, `gobj(...,1)`), the old kernel scene array `SCENE_STATIC` (for the read test only), the shader state buffer (4 pages) and two 256x128 test images. Engine objects (status page, 4-page ring, context) are added when each engine is set up |
| 5 | The per-context page table (legacy 32-bit: one page directory used for all four PDPs) also maps the two screen buffers (uncached, PAT index 3); every other object is snooped (index 0). Everything is mapped before any engine runs, so no GPU TLB invalidation is needed |
| 6 | Context image for the blitter engine (base 0x22000): two `MI_LOAD_REGISTER_IMM` blocks with context control (restore inhibit), ring head/tail/start/control, batch-buffer registers, timestamp and the four PDPs. Engine set up: interrupts masked, status page address (0x80), STOP_RING cleared (0x9C), execlists enabled (`RING_MODE` 0x29C), context status buffer read pointer reset |
| 7 | First submission through the execlist submit port (0x230, descriptor: valid, legacy 32-bit addressing, privileged): `XY_COLOR_BLT` of 64x64 pixels into the test buffer, then `MI_FLUSH_DW` writing a sequence number into the status page. Waits up to 50 ms for the number, then for an idle event in the context status buffer |
| 8 | Checks the filled pixels, then an `XY_SRC_COPY_BLT` inside the test buffer and checks the copy |
| 9 | Read tests (`rd_tests()`): for the test buffer (DMA arena), the new scene buffer (heap) and the old kernel array, the CPU writes 64 pixels and the blitter copies them into the test buffer, first after a `clflush` of those lines, then without. The flushed test runs first so a line the GPU read earlier cannot hide a wrong address. `gpuinfo` shows ok, wrong (with the value read) or no answer for each of the six tests. The new scene buffer must pass the flushed test; if it also passes the unflushed one the GPU snoops, otherwise every GPU screen copy first flushes the scene from the CPU caches |
| 10 | Blitter usable; the render engine and the shaders are started (below), then `scene_swap()` copies the picture into the new buffer and points `SCENE` at it |
| 11 | Ready. Five full-screen copies by the CPU and five by the GPU are timed; screen updates use whichever is faster |

Why the scene moved: on the Lenovo (v9.67) the GPU read the same old pixel (0x005B6198) with and without a cache flush, so the problem was the address, not caching. `SCENE` was a 8 MB array in the kernel's `.bss` above 0x10000000, where `early_remap()` can move 2 MB pages that the firmware reports as not RAM. `SCENE` is now a pointer (`u32* SCENE`, initially `SCENE_STATIC`), and on gen 9 it is switched to a heap buffer whose pages the GPU driver mapped itself. `gpuinfo` shows the physical address of both buffers and whether the old one was moved at boot. Nothing else in the kernel keeps a copy of the pointer.

Every submission resets the ring head to 0 and sets the tail in the saved context, so the ring never wraps. After the first run the context's restore-inhibit bit is cleared, so later runs restore the engine state the hardware saved.

Render engine (base 0x2000, started after the blitter is ready, `rcs_init()`): the same submission code with a 23-page context (status page plus 22 pages of render state) whose image adds `BB_PER_CTX_PTR`, `INDIRECT_CTX`, `INDIRECT_CTX_OFFSET` and a third `MI_LOAD_REGISTER_IMM` for `R_PWR_CLK_STATE` (0x20C8). On Apollo Lake and Gemini Lake the value is computed like Linux does: the enabled subslices from `FUSE2` (0x9120, bits 22:20 are disable bits) and the enabled EUs per subslice from 0x9134 (8 bits per subslice, 6 used), giving enable (bit 31), subslice count enable (bit 11), the subslice count (bits 10:8) and the minimum and maximum EU count (bits 3:0 and 7:4); a full UHD 605 gives 0x80000B66. Without this request power gating can leave EUs switched off. Other chips keep 0. Completion is signalled by `PIPE_CONTROL` (CS stall, render-target and data-cache flush, post-sync write of the sequence number to the status page through the GGTT). Steps: 7 `MI_STORE_DATA_IMM` into the status page and read back; 8 `PIPE_CONTROL` flush then `PIPELINE_SELECT` GPGPU (0x69040302); 9 `STATE_BASE_ADDRESS` (all bases 0, sizes maximal); 10 `MEDIA_VFE_STATE` (32 threads, 4 URB entries); 11 ready; then the shaders below are started. While the GPU path is used, `blit_all()` has the blitter copy `SCENE` into the hidden buffer and then flips; three failed copies turn it off and the CPU copy takes over. `gpu blit gpu` and `gpu blit cpu` choose by hand. QEMU has no Intel graphics, so only the fallback was tested.

GPU instruction assembler (`drivers/video/intel/gen9asm.c`, stage 2). It encodes native 128-bit gen 9 EU instructions in align1 mode, never compacted, with NoMask: `mov`, `and`, `or`, `shr`, `shl`, `add` with register regions (`<8;8,1>`, scalars `<0;1,0>`) and 32-bit or packed-vector (`:uv`) immediates, and `send` with an immediate message descriptor and the end-of-thread bit. `g9_kernel_fill()` and `g9_kernel_copy()` build the two first shaders. Both were checked bit for bit against Intel's own assembler (IGA, platform gen 9) and its semantic checks (`-Wall`) during development.

| Shader | What one thread does (one 8x8 pixel block, SIMD8, one thread per thread group) |
|---|---|
| fill | Block position from the thread group IDs in r0.1 and r0.6. Lane numbers from `0x76543210:uv`. Pixel = red `(x >> sx) & 255`, green `(y >> sy) & 255`, blue from the constant data in r1.0 (`sx` and `sy` in r1.1 and r1.2). Eight rows in r11-r18 are written with one media block write (32 bytes x 8 rows, header in r10, data port 1, binding table entry 0) |
| copy | Same header; one media block read from entry 1 into r11-r18, then a media block write of those registers to entry 0 |

Both end by copying r0 to r127 and sending it to the thread spawner with the end-of-thread bit.

Shader state (`sh_init()`, one 16 KB buffer that is the surface, dynamic and instruction base): constant data at 0x40 and 0x80, binding tables at 0x100/0x120/0x140, surface states (2D, linear, B8G8R8A8, pitch and size of the image) at 0x200/0x240/0x280, interface descriptors (kernel offset, single program flow, binding table, one constant register, one thread) at 0x400/0x440/0x480, the fill kernel at 0x1000 and the copy kernel at 0x1800.

One dispatch (`sh_run()`): `PIPE_CONTROL` (CS stall, render-target and data-cache flush), `PIPELINE_SELECT` GPGPU, `STATE_BASE_ADDRESS` (all bases at the state buffer), `PIPE_CONTROL` invalidating the instruction, texture, constant and state caches, `MEDIA_VFE_STATE` (64 threads, 2 URB entries, 2 registers of constant space), `MEDIA_CURBE_LOAD` (64 bytes), `MEDIA_INTERFACE_DESCRIPTOR_LOAD` (one descriptor), `GPGPU_WALKER` (SIMD8, width/8 x height/8 thread groups, right mask 0xFF), `MEDIA_STATE_FLUSH`, a `PIPE_CONTROL` with CS stall and data-cache flush, then the usual completion `PIPE_CONTROL`.

| Step | Shader start-up |
|---|---|
| 12 | Kernels assembled into the state buffer, surfaces, binding tables and descriptors written |
| 13 | The fill shader runs over a 256x128 test image (512 thread groups) |
| 14 | Every pixel of the image is checked against the same formula on the CPU; the first wrong pixel is reported with its position, value and expected value |
| 15 | The copy shader copies the image into a second one; every pixel is checked |
| 16 | The fill shader draws a full-screen gradient into the new scene buffer before it is used and is timed; every 7th pixel of every row is checked. The picture is then replaced by the real screen contents, so it is never shown |
| 17 | Ready: `gpuinfo` shows "Shaders: ready" with the times |

If a shader does not finish within 50 ms the render engine is marked stopped and the scene buffer is not switched, so a late GPU write cannot reach the screen. The blitter and CPU drawing are not affected. QEMU has no Intel graphics, so only the fallback was tested.

GPU drawing (stages 3 to 5: `drivers/video/intel/gpukern.c`, `gq.c`, `gfx.c`). `gpukern.c` builds 12 shaders with the assembler; all share a prologue that computes the 8x8 block, a per-pixel mask for the rectangle edges and the media block reads, and an epilogue that writes the block. Constant data per operation holds the block origin, the negated rectangle, colours, alpha, source and mask offsets, gradient step and colour key.

| Shader | Pixel result |
|---|---|
| fill, blend | colour; colour mixed with alpha |
| mask | colour mixed with alpha times a coverage value read from a mask surface (text, rounded corners, circles) |
| copy, copy with mask | source pixel mixed with alpha (times mask); with colour key, transparent source pixels are skipped |
| gradient H/V | two colours mixed along x or y |
| tint | night light (green 15/16, blue 5/8) |
| blur H/V | average of 8 neighbours along x or y |
| over, over with mask | premultiplied source over destination (window layers) |

Draw queue (`gq.c`): surfaces are registered with their CPU pointer, GPU address, size and pitch (one surface state each). An operation is clipped to the destination, then recorded: binding table, interface descriptor and constant data in the state buffer, `MEDIA_CURBE_LOAD`, `MEDIA_INTERFACE_DESCRIPTOR_LOAD`, `GPGPU_WALKER`, `MEDIA_STATE_FLUSH`. Written and read rectangles (block-aligned, reads clamped to the source surface, blur +-4) are tracked per surface; when a new operation overlaps an earlier write, or writes where an earlier operation reads, a `PIPE_CONTROL` CS stall is inserted. A batch holds up to 1600 operations or about 12 million pixels. `gq_sync()` submits and waits; if a batch fails, the queue switches to CPU execution at once and GPU drawing stops. A CPU executor with exactly the same arithmetic is used in emulated mode, as the reference in the self-test, and after a failure.

Drawing layer (`gfx.c`): text is collected in runs and pixels in a batch; both are written by the CPU into staging surfaces (coverage and colour) and drawn by one masked operation. Rounded corners and circles come from a mask cache. `SCENE` is a macro that waits for queued GPU work before the CPU touches the screen buffer; `putpx` and `pblend` go into the pixel batch. Window layers: one full-screen surface per window, cleared to transparent; the window's drawing code runs with the layer as its target; the touched area is remembered. The compositor draws the shadow, glass behind (blurred scene), the title strip (blurred, slightly transparent, top corners rounded) and the body (rounded corners, alpha), then any part the window drew outside its frame. While a window is dragged without resizing, only this step runs. Smooth scrolling keeps a snapshot of the scrolled area; for 140 ms the new contents slide in and the snapshot fills only the uncovered strip. Closing and minimising keep the layer as a fading ghost.

Fewer operations per frame (v9.71): text from anywhere in a window (any line, any colour) goes into one batch with a bounding box and is drawn with one masked operation when the window finishes, when the batch fills (4096 glyphs) or when another operation overlaps it. The staging area is rewound at the start of every frame. Each window's operations are recorded into a list (`gq_cap_begin` / `gq_cap_end`); text and pixel batches carry a key computed from their contents. The next frame's list is compared with the previous one: if the window did not move or resize, only the union of the operations that differ is cleared and redrawn (clipped), and an unchanged window is not drawn at all. If code inside the window touches the screen buffer directly, recording is abandoned and the window is redrawn in full for that frame. Shadows are drawn from a precomputed 9-piece shadow image (4 corners and 4 edge strips) with 8 masked operations; very large, very rounded or translucent windows use the old per-pixel shadow. The blurred glass behind a window is kept in a per-window cache keyed by a hash of every operation drawn into the scene before it and the window position; while the key is the same it is copied instead of blurred again. Wallpaper scaling now interpolates between pixels (bilinear); this runs once on the CPU when the wallpaper or screen size changes, not as a shader (a shader would need a per-pixel gather from computed addresses, which the existing block-read shaders do not do and which could not be checked against the hardware before release).

GPU clock (`igpu.c`): on Gemini Lake and Apollo Lake the allowed range is read from `RP_STATE_CAP` (0x138170, units of 16.67 MHz). When GPU drawing starts, the frequency request (`RPNSWREQ`, 0xA008) is set to the maximum; when drawing stops for any reason the original request is restored. `gpuinfo` shows the current frequency (`RPSTAT1`, 0xA01C), the requested frequency and the range.

Partial screen rebuild (v9.72, `gq_srec_begin` / `gq_srec_end` in `gq.c`): during a desktop frame every operation that writes the screen, or reads a buffer written that way, is recorded instead of run; operations on window layers still run at once. At the end of the frame the list is lined up with the previous frame's list (operation hashes, look-ahead of 96). A forward pass finds what changed per buffer: unmatched operations, and matched operations whose inputs changed (changes to layers and other buffers since they were last read are tracked, reads past an edge count as reads of the edge). The screen area to rebuild is the bounding rectangle of the changes plus anything written outside a frame. A backward pass marks which operations on scratch buffers are needed; any operation that reads the screen must have its whole read area inside the rebuilt area, which is grown until that holds, and later writes to a buffer whose earlier write was re-run are re-run too. Then the marked operations run in order, screen operations clipped to the area. Anything that reads or writes the screen with the CPU during the frame, a buffer that is changed after a recorded operation read it, or more than 4096 operations make the frame a full one. `gpuinfo` shows partial, full and unchanged frames, skipped frames, the last area and the reason for the last full rebuild.

Frame check (`gpu check`, also run once when the desktop starts with GPU drawing): interrupts are paused, the frame is drawn on the GPU with every screen operation logged, then drawn again by the CPU executor, and the two are compared (colour only). The result and, on a difference, the first wrong pixel and the operation that last wrote it are shown in `gpuinfo`; a second run with a wait between operations decides whether the slower mode is kept.

Frame breakdown in `gpuinfo`: CPU time spent recording the frame, time waiting for the GPU to finish drawing, time for the screen copy, submissions, operations, ordering waits, text operations and characters, windows redrawn (full, partly, unchanged, by the CPU, with the reason for the last full CPU-style redraw), blur cache hits and misses, fast and slow shadows.

Start-up (`q_init()` and `q_start()` in `igpu.c`): step 18 shaders assembled; 19 four test images (rows padded to 64 bytes); first fill, a read test (the GPU must see two different values the CPU wrote to the same place, so no stale GPU cache) and a write test (the CPU must see what the GPU drew); 20 the self-test (400 random operations, every pixel compared with the CPU reference; on a mismatch the batch is restored and rerun one operation per submission: if that is correct, drawing continues with a CS stall between every operation (`g_gq_serial`), otherwise the first wrong operation is reported in `gpuinfo`); 21 screen and display surfaces; 22 drawing buffers (wallpaper, cursor cache, two blur buffers, staging, mask cache); 23 the CPU, blitter and shader screen copies are timed and the fastest is used. `gpuinfo` shows the step and reason if it stops, the last frame time, operations, ordering waits and CPU waits, layers and their memory, and the raw fuse registers.

### 10.10.5 NVIDIA (`drivers/video/nvidia/`)

None of this code runs at boot. It is reached only through the `gpu`/`nvidia`, `gpumon` and `gpuedid` commands.

| Implemented | Where |
|---|---|
| PCI probe for vendor 0x10DE class 03; BAR0/BAR1/BAR3 with 64-bit BAR1 handling; BAR0 size by sizing write; enable MEM+BM | `nv_probe` |
| `PMC_BOOT_0` read to architecture name (Fermi ... Ada) | `nv_read_id`, `nv_arch_name2` |
| VBIOS copy from PROM (BAR0+0x300000, up to 64 KiB) with the ROM shadow bit in 0x088050 cleared during the read | `nv_vbios_from_prom` |
| BIT signature search; DCB 4.x located by signature 0x4EDCBDCB; DCB entries to connector types | `nv_find_bit`, `nv_find_dcb`, `nv_parse_dcb` |
| Output list from the DCB plus connector table plus I2C table (type, heads, SOR, connector type, DDC port) | `nvmon.c: nvm_outputs` |
| Read-only state of the display engine already set up by firmware: heads from 0x610060/0x610074, per-head timings from 0x682064...+0x8000+h*0x400 to active mode and Hz, SOR to head protocol from 0x680300 | `gpu` command, `nvm_decode_head` |
| EDID by bit-banged DDC on the PMGR I2C port `0xD014 + port*0x20`, with clock stretching bounded at 2200 us | `nvm_read_edid`, `nvm_edid_parse` (`gpumon`) |

| Not implemented | Detail |
|---|---|
| Mode setting | `nv_modeset_scaffold()` pushes placeholder method numbers into a buffer that is never submitted. It and `g_nv_modeset_enable` are never referenced. `gpu` prints "modeset: disp907d scaffold present but disabled" |
| DisplayPort AUX | `gpumon` reports "DisplayPort monitors need the AUX channel - not supported yet" |
| Acceleration, memory management, firmware (GSP/falcon) loading | for Turing in v9.81: section 10.10.6 |
| Power management | absent |
| Older EDID path | `gpuedid` uses `nvdisp.c`'s bit-bang at the unverified register 0x00E138 and warns that it is experimental |
| `nv_init()` | defined but never called |

### 10.10.6 NVIDIA GSP-RM for Turing (`nvflcn.c`, `nvmem.c`, `nvgsp.c`)

Added in v9.81. It follows the boot order of the Linux nouveau r535 code and uses NVIDIA's signed firmware 535.113.01 from linux-firmware (`assets/nvidia/`, xz-compressed and linked in by `build.sh`; files with the same names in Files, for example `booter_load-535.113.01.bin`, are used first). Nothing runs at boot; the `nvgsp` command drives it, writes each step to a log and saves the log as `nvgsp-log.txt` in Files.

| Command | What it does |
|---|---|
| `nvgsp` or `nvgsp status` | state, last step reached, the reason for a failure |
| `nvgsp vram` | finds the card, sizes the BARs, waits for GFW_BOOT, reads the VRAM size (0x100CE0) and the VGA workspace (0x625F04) and prints the WPR2 layout; touches nothing |
| `nvgsp boot` | steps 1-10 below: GSP-RM running, BAR2 and BAR1 working |
| `nvgsp objects` | steps 11-15: the objects and the copy-engine test |
| `nvgsp all` | boot, then objects |
| `nvgsp unload` | UNLOADING to GSP-RM, then booter_unload on SEC2, which clears WPR2 |
| `nvgsp log` | the log of the last command |

| Step | Work | Where |
|---|---|---|
| 1 | PCI probe (class 03, vendor 0x10DE), memory and bus master on, legacy interrupt off, Turing check from PMC_BOOT_0 | `probe` |
| 2 | GFW_BOOT wait, VRAM size, WPR2 layout: FRTS (1 MB below the VGA workspace), GSP bootloader, the GSP ELF, the GSP-RM heap, WPR2 and the non-WPR area under it | `nv_gfw_wait`, `nv_fb_layout` |
| 3 | GSP firmware: ELF sections `.fwimage` and the chip's signature section, radix3 page tables for the 24 MB image in system memory, LibOS arguments, the RM arguments, the log buffers and the shared message queues (command queue at 0x1000, message queue at 0x41000, 63 slots each, XOR checksum); SET_SYSTEM_INFO and SET_REGISTRY are queued before the boot | `prepare` |
| 4 | VBIOS ROM read (1 MB, IFR header skipped), FWSEC found through the BIT 'p' table (application 0x85), patched for FRTS (or SB) and run on the GSP falcon (0x110000) through `acr/bl.bin`; then WPR2 must be where FRTS was asked for | `nvb_find_fwsec`, `nv_run_fwsec` |
| 5 | booter_load on SEC2 (0x840000) with the WPR metadata address in the mailboxes | `nv_booter` |
| 6 | GSP RISC-V core running | `boot_gsp` |
| 7 | GSP_INIT_DONE event (the GSP sequencer events are run on the way) | `rpc_wait`, `seq_run` |
| 8 | GET_GSP_STATIC_INFO: the usable VRAM regions and BAR1 page directory base; the VRAM allocator gets those regions with the console framebuffer reserved | `vram_alloc_init` |
| 9 | BAR2: page tables in VRAM, UPDATE_BAR_PDE, write and read back a test page through the window | `bars` |
| 10 | BAR1: page tables under the base GSP-RM reports, the console framebuffer mapped 1:1, read back through the window | `bars` |
| 11 | FIFO device info: which runlists the GR engine and the copy engine use | `fifo_info` |
| 12 | Golden context: a first GR channel with COPY_SERVER_RESERVED_PDES and PROMOTE_CTX, as GSP-RM needs | `golden` |
| 13 | client (NV01_ROOT, 0x0000), device (0x0080), subdevice (0x2080), address space FERMI_VASPACE_A (0x90F1) with its page directory | `objects` |
| 14 | channels (TURING_CHANNEL_GPFIFO_A, 0xC46F) for GR and the copy engine, context buffers promoted, then `TURING_A` (0xC597), `TURING_COMPUTE_A` (0xC5C0) and `TURING_DMA_COPY_A` (0xC5B5) | `user_chan`, `objects` |
| 15 | copy-engine test: a push buffer with SET_OBJECT and LAUNCH_DMA copies a 4 KB pattern in VRAM, kicked through the work-submit token and the doorbell | `ce_test` |

Memory: `nvv_*` is an extent allocator over VRAM (256 free ranges, 1024 allocations, 4 KB granularity, never returns address 0, clears what it hands out). `nvm_*` builds GPU page tables in the version 2 format (PD3, PD2, PD1, dual PD0, 4 KB small page tables), joins an address space to an instance block (page directory at instance+0x200) and flushes the GPU TLB (0xB830A0). VRAM is reached through the PRAMIN window (0x1700, 0x700000).

Copy engine for other code (v9.82): `nvgsp_ce_ready()` says whether the channels exist; `nvgsp_ce_map(kva, len)` maps kernel memory into the GPU address space page by page through the CPU page tables (`kva_to_pa`, so it also works for pages `early_remap` moved); `nvgsp_ce_copy2d(src, src_pitch, dst, dst_pitch, bytes_per_line, lines)` pushes one pitched multi-line copy with a semaphore release and waits up to 200 ms for it. The Gears app uses it to put frames on the screen. The simulator checks a 300x170 copy into a pitched frame and that nothing outside it changes.

Limits: untested on a real card; only the simulator (`tools/tests/nvidia/nvsim.c`, `check.py nvidia`) has run it. The firmware has no TU117 booter, so a TU117 uses the TU116 one unless `booter_load-535.113.01.bin` for TU117 is put in Files. FWSEC cannot run twice: if WPR2 is still set from an earlier try the driver stops and asks for a full power-off. After `nvgsp boot` a monitor on the NVIDIA card may go dark, because GSP-RM takes over the display engine and there is no mode setting yet. No interrupts are used; every wait polls with a time limit.

## 10.11 CPU information (`arch/x86_64/cpu/cpuinfo.c`)

The file defines the `cpuid()` and `rdmsr()` inline wrappers that the rest of the kernel uses (`tp_is_amd`, `arch_fpu_init`, timers). `get_brand()` reads leaves 0x80000002-4 into `g_brand[52]` and strips leading spaces, or uses "x86-64 CPU" when the extended leaves are missing. It sets `g_lma` from IA32_EFER (MSR 0xC0000080) bit 10. The `cpu`/`cpuid` command prints these two values. The later `cores`/`cpu` command branch (SMP view) responds only to `cores`, because the earlier `seq(cmd,"cpu")` takes `cpu`. Frequency, cache and feature-flag reporting are not in this file. TSC/HPET calibration lives in `hrtimer.c` (`timers` command).

## 10.12 Summary of bounded waits

| Driver | Wait | Bound |
|---|---|---|
| PS/2 | controller IBF/OBF | 200000 `io_wait` per byte |
| DW I2C | enable/disable; transfer; bus idle | 100000; 400000 iterations; 100000 |
| I2C-HID | reset completion | 200x5 ms |
| xHCI | handoff; CNR/halt/reset; any event | 400000; 200000-400000; 400000 polls |
| USB hub | port reset | 100x5 ms |
| CMOS | update-in-progress | 100000 reads |
| ACPI | ACPI-mode switch; Global Lock; EC byte; whole AML batch | 1 s; 50 ms; 20000 `io_wait` plus a 5 s breaker; 15 s init / 500 ms per poll |
| AC'97 / HDA | codec ready; CRST and stream reset; verb | 200 ms; 10 ms; 200 ms |
| `/dev/dsp` | ring full | 2000x1 ms |
| NVIDIA DDC | SCL stretch | 2200 us per bit |
