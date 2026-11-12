#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/edid.h"

static mon_edid_t g_mon;
static u8 g_mon_raw[128];

static int ed_dtd(const u8* d, u32* w, u32* h, u32* mhz){
    u32 clk = (u32)(d[0] | (d[1] << 8));
    if(!clk) return 0;
    u32 ha = (u32)(d[2] | ((d[4] & 0xF0) << 4)), hb = (u32)(d[3] | ((d[4] & 0x0F) << 8));
    u32 va = (u32)(d[5] | ((d[7] & 0xF0) << 4)), vb = (u32)(d[6] | ((d[7] & 0x0F) << 8));
    u64 tot = (u64)(ha + hb) * (u64)(va + vb);
    if(!ha || !va || !tot) return 0;
    if(d[17] & 0x80) tot /= 2;
    *w = ha; *h = va; *mhz = (u32)(((u64)clk * 10000ULL * 1000ULL + tot / 2) / tot);
    return 1;
}

static void ed_text(const u8* p, char* out, int cap){
    int n = 0;
    for(int i = 0; i < 13 && n < cap - 1; i++){ u8 c = p[i]; if(c == 0x0A || c == 0) break; out[n++] = (c >= 32 && c < 127) ? (char)c : '?'; }
    while(n > 0 && out[n - 1] == ' ') n--;
    out[n] = 0;
}

int edid_parse(const u8* e, mon_edid_t* m){
    for(u32 i = 0; i < sizeof(*m); i++) ((u8*)m)[i] = 0;
    static const u8 hdr[8] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};
    for(int i = 0; i < 8; i++) if(e[i] != hdr[i]) return 0;
    u8 sum = 0; for(int i = 0; i < 128; i++) sum = (u8)(sum + e[i]);
    if(sum) return 0;
    u16 id = (u16)((e[8] << 8) | e[9]);
    m->maker[0] = (char)('A' - 1 + ((id >> 10) & 31)); m->maker[1] = (char)('A' - 1 + ((id >> 5) & 31)); m->maker[2] = (char)('A' - 1 + (id & 31)); m->maker[3] = 0;
    for(int i = 0; i < 3; i++) if(m->maker[i] < 'A' || m->maker[i] > 'Z') m->maker[i] = '?';
    m->product = (u16)(e[10] | (e[11] << 8));
    m->year = e[17] ? 1990 + e[17] : 0;
    m->ver = e[18]; m->rev = e[19];
    m->digital = (e[20] & 0x80) ? 1 : 0;
    m->iface = m->digital && m->ver == 1 && m->rev >= 4 ? (e[20] & 0x0F) : 0;
    m->bits = m->digital && m->ver == 1 && m->rev >= 4 ? (((e[20] >> 4) & 7) ? 4 + 2 * ((e[20] >> 4) & 7) : 0) : 0;
    m->cm_w = e[21]; m->cm_h = e[22];
    for(int k = 0; k < 4; k++){
        const u8* d = e + 54 + k * 18;
        if(d[0] || d[1]){
            u32 w, h, mhz;
            if(ed_dtd(d, &w, &h, &mhz)){
                if(!m->nat_w){ m->nat_w = w; m->nat_h = h; m->nat_mhz = mhz; }
                if(w == m->nat_w && h == m->nat_h && mhz > m->best_mhz) m->best_mhz = mhz;
            }
            continue;
        }
        if(d[3] == 0xFC) ed_text(d + 5, m->name, (int)sizeof(m->name));
        else if(d[3] == 0xFF) ed_text(d + 5, m->serial, (int)sizeof(m->serial));
        else if(d[3] == 0xFD){ u32 lo = d[5], hi = d[6]; if(m->ver == 1 && m->rev >= 4){ if(d[4] & 1) lo += 255; if(d[4] & 2) hi += 255; } m->range_lo = lo; m->range_hi = hi; }
    }
    if(!m->best_mhz) m->best_mhz = m->nat_mhz;
    m->ext = e[126];
    m->ok = m->nat_w ? 1 : 0;
    return m->ok;
}

void edid_boot_capture(void){
    volatile u8* p = (volatile u8*)0x5200ULL;
    if(*(volatile u32*)p != 0x31444945u) return;
    for(int i = 0; i < 128; i++) g_mon_raw[i] = p[4 + i];
    edid_parse(g_mon_raw, &g_mon);
    g_mon.src = *(volatile u8*)(p + 132);
}

const mon_edid_t* mon_get(void){ return &g_mon; }
const u8* mon_raw(void){ return g_mon.ok ? g_mon_raw : 0; }

int mon_cur_mhz(u32 w, u32 h){
    if(!g_mon.ok || w != g_mon.nat_w || h != g_mon.nat_h) return 0;
    return (int)g_mon.nat_mhz;
}

int mon_max_hz(void){
    if(!g_mon.ok) return 0;
    u32 hz = (g_mon.best_mhz + 500) / 1000;
    if(g_mon.range_hi > hz) hz = g_mon.range_hi;
    return (int)hz;
}

const char* mon_iface_name(void){
    static const char* const N[] = {"", "DVI", "HDMI", "HDMI", "MDDI", "DisplayPort"};
    if(!g_mon.ok) return "";
    if(!g_mon.digital) return "VGA (analog)";
    return g_mon.iface >= 1 && g_mon.iface <= 5 ? N[g_mon.iface] : "digital";
}
