#ifndef GQK_HOST
typedef unsigned int u32;
typedef unsigned long long u64;
#define GQK_ENTRY __attribute__((nvptx_kernel))
#define GQK_P(va) ((u32*)(va))
#define GQK_CX __nvvm_read_ptx_sreg_ctaid_x()
#define GQK_CY __nvvm_read_ptx_sreg_ctaid_y()
#define GQK_NX __nvvm_read_ptx_sreg_ntid_x()
#define GQK_NY __nvvm_read_ptx_sreg_ntid_y()
#define GQK_TX __nvvm_read_ptx_sreg_tid_x()
#define GQK_TY __nvvm_read_ptx_sreg_tid_y()
#endif

typedef struct { u64 va; u32 pitch, w, h, pad; } nvs;
typedef struct { u32 k, x, y, w, h; int sdx, sdy, mdx, mdy; u32 c0, c1, a, inv, key, goff, pad; nvs d, s, m; } nvop;

static u32 bl(u32 d, u32 s, u32 a) {
    u32 ia = 256 - a;
    u32 rb = (((s & 0x00FF00FFu) * a + (d & 0x00FF00FFu) * ia) >> 8) & 0x00FF00FFu;
    u32 ag = (((s >> 8) & 0x00FF00FFu) * a + ((d >> 8) & 0x00FF00FFu) * ia) & 0xFF00FF00u;
    return rb | ag;
}

static u32 over(u32 d, u32 s) {
    u32 t = s >> 24;
    t += t >> 7;
    u32 rb = (((d & 0x00FF00FFu) * t) >> 8) & 0x00FF00FFu;
    u32 ag = (((d >> 8) & 0x00FF00FFu) * t) & 0xFF00FF00u;
    return s + rb + ag;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static u32 rd(u64 va, u32 pitch, u32 w, u32 h, int x, int y) {
    x = clampi(x, 0, (int)w - 1);
    y = clampi(y, 0, (int)h - 1);
    const u32* p = GQK_P(va);
    return p[(u64)y * pitch + (u32)x];
}
#define RDS(xx, yy) rd(o.s.va, o.s.pitch, o.s.w, o.s.h, (xx), (yy))
#define RDM(xx, yy) rd(o.m.va, o.m.pitch, o.m.w, o.m.h, (xx), (yy))

GQK_ENTRY void gqk(nvop o) {
    int gx = (int)(GQK_CX * GQK_NX + GQK_TX);
    int gy = (int)(GQK_CY * GQK_NY + GQK_TY);
    if(gx >= (int)o.w || gy >= (int)o.h) return;
    int x = (int)o.x + gx, y = (int)o.y + gy;
    if(x < 0 || y < 0 || x >= (int)o.d.w || y >= (int)o.d.h) return;
    u32* row = GQK_P(o.d.va) + (u64)y * o.d.pitch;
    u32 d = row[x], n = d;
    switch(o.k) {
    case 0: n = o.c0; break;
    case 1: n = bl(d, o.c0, o.a); break;
    case 2: { u32 m = RDM(x + o.mdx, y + o.mdy); n = bl(d, o.c0, (m * o.a) >> 8); } break;
    case 3: case 4: {
        u32 s = RDS(x + o.sdx, y + o.sdy);
        if(o.key && (s >> 24)) return;
        u32 a = o.a;
        if(o.k == 4) { u32 m = RDM(x + o.mdx, y + o.mdy); a = (m * o.a) >> 8; }
        n = bl(d, s, a);
    } break;
    case 5: case 6: { u32 dv = (u32)((o.k == 5 ? (x - (int)o.x) : (y - (int)o.y)) + (int)o.goff); u32 t = (dv * o.inv) >> 8; n = bl(o.c0, o.c1, t); } break;
    case 7: n = (d & 0xFFFF0000u) | (((((d >> 8) & 255) * 15) >> 4) << 8) | (((d & 255) * 5) >> 3); break;
    case 8: case 9: {
        u32 rb = 0, g = 0;
        for(int k = -4; k < 4; k++) {
            u32 s = o.k == 8 ? RDS(x + o.sdx + k, y + o.sdy) : RDS(x + o.sdx, y + o.sdy + k);
            rb += s & 0x00FF00FFu;
            g += (s >> 8) & 0x00FF00FFu;
        }
        n = ((rb >> 3) & 0x00FF00FFu) | ((g << 5) & 0xFF00FF00u);
    } break;
    case 10: case 11: {
        u32 s = RDS(x + o.sdx, y + o.sdy);
        u32 a = o.a;
        if(o.k == 11) { u32 m = RDM(x + o.mdx, y + o.mdy); a = (m * o.a) >> 8; }
        n = bl(d, over(d, s), a);
    } break;
    default: return;
    }
    row[x] = n;
}
