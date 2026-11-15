# v2 test plan for the owner: about 5 minutes on the real PC (v11.02 or v11.03)

Take a photo of the screen any time something looks odd. One photo is enough for each step.

## How to read the results (for the lead)
- Pointer moves, clock ticks, only the window is stuck: an app's redraw after the drag (Files/Settings/Terminal/Browser draw code). Reports s1, h3.
- Pointer and keys dead, but the top-bar clock still ticks: the mouse USB endpoint died, the OS is still running. Reports s3, h5, a3.
- Everything stops but the red "NoovexOS is not responding" box shows up within about 5 to 10 s: the main loop is stuck. The box gives the stage number (7 = input, 13 = drawing, 8 = USB). Reports s5, h6.
- Everything stops with no red box after 30 s and Caps Lock does not light: a hard hang (IRQs off or BKL stuck, the watchdog can't see it). Reports s5, h6.
- It only happens with the camera or USB stick plugged in: the USB device path (camera/mic start, crash file saving to the stick). Report s2.
- It only happens when snapping to an edge: apply_snap and the resize redraw (h2).

## Steps

1. **Before testing.** Plug everything in the way it normally is (USB stick, camera, mic). Open Terminal from the Start menu and type these. Take a photo after each one:
   `usbinfo`, then `usbhid`, then `lag`, then `crash`.
   (`crash` shows the last saved freeze or crash report. If it says "No crash or freeze report", that is fine.)

2. **Easy drag, no snap.** Open **Files** from the Start menu. Drag the window by its title bar to the middle of the screen and let go. Wait 10 s.
   Look at: does the pointer still move? Does the clock in the top bar change? Does pressing Caps Lock turn the keyboard light on and off?

3. **Snap drag.** If step 2 did not freeze: drag the Files window to the **top** edge of the screen until it snaps big, then let go. Wait 10 s. Check the same things (pointer, clock, Caps Lock).

4. **Other apps.** If nothing froze yet, do steps 2 and 3 again with **Settings**, then **Terminal**, then **Task Manager** (it redraws all the time, so it acts differently). Write down which app froze and whether it was the snap or the plain drag.

5. **When it freezes: wait 60 s.** Don't press reset right away. Watch for a **red "NoovexOS is not responding" box**. If it shows, take a photo of the whole box (it has a "stage" number and a list of functions). Also check: pointer moves? clock ticks? Caps Lock light works? Then photograph the screen once more at 60 s.

6. **Restart and read the report.** Reset the PC. Once you are on the desktop, a notice "NoovexOS stopped last time" may show and open the report. Either way, open Terminal and type `crash`, then `lag`, then `usbhid`. Photograph each one. If the USB stick was in, it may also have a file called **CRASH.TXT** on it. Send it if it's there.

7. **Without the camera and USB stick.** Unplug the USB camera and the USB stick (leave the keyboard/mouse receivers in). Restart. Repeat the step that froze (same app, same snap or no snap) 3 times. Did it still freeze? Yes or no.

8. **Mouse check (only if the pointer died but the clock kept ticking).** After a restart, type `usbhid live` in Terminal and move the mouse for a few seconds, clicking once and letting go. Photograph the screen. Also try unplugging the mouse receiver and plugging it back in after a freeze. Does the pointer come back?

## What to send back (short is fine)
- Which app, snap or no snap, camera/stick in or out.
- Pointer yes/no, clock yes/no, Caps Lock light yes/no, red box yes/no (with photo).
- Photos of `crash`, `lag`, `usbhid` and `usbinfo`.

## The real commands (checked in shell/commands)
- `usbinfo`: the USB controllers and devices (network_and_apps.c:213). `usb` / `xhci` rescans the controllers and then runs usbinfo. Avoid this during a test because it changes the state.
- `lsusb` / `usbdevs`: short device list with VID:PID and port.
- `usbhid`: the HID layout and the last report for the keyboard/mouse. `usbhid live` shows mouse reports live for 15 s (shell/commands/usbhid.c:55).
- `lag`: the longest run of each main-loop part since the last `lag` (core/init/main.c:192).
- `crash`: shows crash-report.txt or the kept freeze report (core/crash.c:348). On a freeze the report is shown after 5 s and saved after about 30 s to CRASH.TXT on the stick and to crash-report.txt (crash.c:162-164, 258-261). On the next boot the "NoovexOS stopped last time" notice opens it (cx_notice).
- `uptime`, `tracelog`, `netlog` also exist. There is no dmesg/klog command. The Logs app shows klog.
- Caveat: if the stick is unplugged and nothing is installed to disk, a freeze report only lives on the screen. That makes the photo from step 5 the key evidence.
