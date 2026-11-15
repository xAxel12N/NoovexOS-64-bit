# R1 review: w-c1 (d7670b35, bef07be0) vs v11.03

Verdict: APPROVE. No HIGH/MEDIUM issues. All bounds checks verified overflow-safe and not off-by-one.

Checked
- woff2: sum/tlen/comp are unsigned, sum<=64M invariant holds so `(64u<<20)-sum` cannot wrap; comp check is overflow-safe; output size in u64 with per-table/total caps. rebuild_hmtx nxm=ng matches xm alloc (ng+1). Normal web fonts unaffected.
- imgx item_data: per-extent off/len vs lim and running 256 MiB budget checked before malloc; behaviour for method 2 unchanged (rejected before too). parse_iloc v2 needs 4+2+4=10 bytes: correct.
- png.h: `clen>len-p-12` correct (negative RHS when 8<=len-p<12 -> break). IHDR clen>=13 needed for data[12]. w/h signed so <=0 catches high-bit values. need as long; later y*(1+stride) stays < rawcap (int), safe.
- svg fill_poly / nwe_acc_line: NaN/inf/|v|>=1e7 rejected; fill_poly already drops shapes >3000 px bbox, so no new rejection of normal SVGs. Early return is before allocation, no leak.
- ttf_outline: ops 0/1 (2 args), 2 (4), 4 (0) match emitter; buf[60000] with cap 59990 so terminator write is always in range.
- BMP off 54..len (V4/V5 headers 122/138 still fine; per-row check still guards). ICO: off<=len && sz<=len-off overflow-safe; sz>=16 covers p[4..15] reads.

LOW
- L1 PNG cap w*h<=64M px rejects e.g. 16384x8192 panoramas (128M px). Likely moot since PNG_RAW_SZ/outcap is far smaller, so no functional change. Optional: drop the w*h term, keep per-dimension 16384:
  `if(w<=0||h<=0||w>16384||h>16384)return -2;`
- L2 IX_MAXPX 8192^2 (67M px) rejects >67 MP AVIF/JXL (medium-format photos). Acceptable memory tradeoff (268 MB RGBA); note in handoff.
- L3 bmp row check computes `d+off+sy*rowsz` past end before comparing (pointer-arith UB, pre-existing). Patch: `if((long)off+(long)sy*rowsz+(long)w*bypp>len)return -1; const u8* row=d+off+(long)sy*rowsz;`
