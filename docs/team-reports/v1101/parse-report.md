# parse — hid_parse audit (branch h-parse, commit c72adfbb)

## Root cause candidate (confidence that it is THE Biltema bug: ~45%; confidence it is a real bug: 99%)
drivers/usb/xhci/controller_init.c hid_parse (~line 128 in HEAD): for a Variable Input with Report Count > number of
Usage items, `u=usages[c<nus?c:nus-1]` repeats the last usage (spec-correct) but every repeat OVERWROTE
`L->y_off`. Usage X, Usage Y, Report Count 3 (or 4) => y_off moved to the trailing unnamed field, which is
always 0 => X decodes, Y is always 0. Reproduced on old code by the new test (dx ok, dy=0).

## Other real bugs fixed in hid_parse
1. Usage Min/Max mixed with plain Usage (Min X, Max Y, Usage Wheel, Count 3): old code ignored the range when any plain
   usage existed => all 3 fields = Wheel => no mouse layout. Now an ordered usage list of {lo,hi} entries (Linux order).
2. Usage Page declared after the usages, before the Main item (Usage X, Usage Y, Usage Page GD): now re-paged at the
   Main item like Linux hid_concatenate_last_usage_page (4-byte usages untouched). Old: no layout.
3. Push/Pop did not save Logical Min (affected consumer cc_base after Pop).
4. A Collection at depth 0 inherited appkind[0] of the previous top-level collection; now kind 0.
Fix: x/y/w offsets are set only by the first field carrying that usage; past the end of a range, fields get usage 0 (unchanged behaviour), past a single usage, the last usage repeats.

## Checked and OK
Collection clears locals (spec-correct; Usage(Pointer) is consumed by Collection(Physical), X/Y come after);
Y in a separate Input; 12-bit packed (Y at 12-23); 16-bit with LMin -32767; 4-byte usages; layout merge by (id,kind)
across Physical collections; ok filter (needs y_off>=0) correct. Not changed: HIDLAY_MAX=4 slots can be eaten by
layouts that are dropped later by the ok filter (low risk).

## Tests
New tools/tests/usb/hidparse_test.sh + hidparse_main.c (reuses hidmouse_gen.py): 13 descriptors, 79 checks asserting
x_off/x_sz/y_off/y_sz/w_off and decoded dx AND dy: ALL PASS with ASan/UBSan. Old code: 12 failures.
Existing hidmouse (26) and hidkbd (38) still ALL PASS. ccheck of drivers/usb/xhci.c: no new warnings.
Not wired into tools/check.py (left to the lead): add like g_usb's hidmouse step with hidparse_main.c as the 5th argument.
To confirm on hardware: dump the Biltema report descriptor (usbinfo) and look for Usage X, Usage Y with Report Count >= 3.
