#ifndef NVX_DRIVERS_INPUT_TP_HID_H
#define NVX_DRIVERS_INPUT_TP_HID_H

#include "include/nvx/base.h"
#include "include/nvx/gui/theme.h"

#define TPF_MAX 10
typedef struct { int off, size, sgn, ok; } tpfld;
typedef struct {
    int uses_ids;
    int ptp, ptp_rid, nfing;
    struct { tpfld tip, conf, cid, x, y; } f[TPF_MAX];
    tpfld cc, scan, btn[3];
    int xmin, xmax, ymin, ymax; u32 xum, yum;
    int mode_rid; tpfld mode; int ccmax_rid; tpfld ccmax; int btype_rid; tpfld btype; int sw_rid; tpfld surf, bsw;
    int feat_len[256];
    int mouse, m_rid; tpfld mbtn[3], mx, my, mw;
    int in_len[256];
} tplay;

int tp_parse(const u8* d,int n,tplay* L);
int tp_bits(const u8* p,int plen,tpfld f);
void tp_setbits(u8* p,int plen,tpfld f,u32 val);

#endif
