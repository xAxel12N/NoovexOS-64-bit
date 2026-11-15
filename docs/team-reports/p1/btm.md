Task btm: MediaTek Bluetooth (MT7921/MT7922/MT7925 USB BT, e.g. 0E8D:0608, 0E8D:0616, 13D3:35xx) firmware download in drivers/bluetooth/.
Source: Linux drivers/bluetooth/btmtk.c/.h — FETCH THE REAL FILE and verify the SPDX line first (scout believed ISC but did not open it);
if it is not ISC/BSD/GPL-2.0-or-later, use facts only and write your own. The scout's struct details were from memory, so take layouts
from the fetched code. Implement the patch header parse, section download via WMT vendor commands over USB control/bulk, and the
function-on step. Hook into btusb.c with a 1–3 line call into a new file mtkbt_fw.c (bti and btb coders hook there too). Inert for
other devices. Firmware from /lib/firmware/mediatek/BT_RAM_CODE_MT79*.bin (do not commit blobs). Host parser test.
