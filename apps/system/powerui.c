#include "include/nvx/base.h"
#include "include/drivers.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/drivers/video/fb/gfx.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/core/power.h"

extern volatile u64 g_ticks;
extern char toast_msg[80]; extern u64 toast_t;
extern int g_gfx, g_gfx_fx;
extern int g_acb_pct, g_acb_charging, g_acb_n, g_ac_online;
extern int g_lid_open;
extern void bl_screen(int on);
extern void blit_all(void);

int g_pm_on, g_screen_off, g_lid_action = 1, g_pbtn_action;
static int pm_sel, pm_bprev, g_scr_drawn;
static u64 pm_t;
static int pm_hx[6], pm_hy[6], pm_hw[6], pm_hh[6], pm_n = 4, pm_menu;
static const char* PM_L[6] = { "Shut down", "Restart", "Sleep", "Log out", "Lock", "Cancel" };
static int pm_act(int i) { return i == pm_n - 1 ? 5 : i; }

static void pm_toast(const char* s) { int i = 0; for (; s[i] && i < 79; i++) toast_msg[i] = s[i]; toast_msg[i] = 0; toast_t = g_ticks; }

void pm_open(void) { g_pm_on = 1; pm_sel = 0; pm_t = g_ticks; pm_bprev = 1; pm_n = 4; pm_menu = 0; }
void pm_open_menu(void) { pm_open(); pm_n = 6; pm_menu = 1; }
void pm_close(void) { g_pm_on = 0; }
static void pm_do(int i) {
    g_pm_on = 0;
    if (i == 0) power_off();
    else if (i == 1) power_reboot();
    else if (i == 2) power_sleep();
    else if (i == 3) { extern void acct_ui_signout(void); acct_ui_signout(); }
    else if (i == 4) { extern void acct_ui_lock(void); acct_ui_lock(); }
}
void pm_power_button(void) {
    { extern int g_sleeping; extern void power_wake_req(void); if (g_sleeping) { power_wake_req(); return; } }
    if (g_state != ST_DESKTOP) { power_off(); return; }
    if (g_screen_off) { g_screen_off = 0; bl_screen(1); return; }
    if (g_pbtn_action == 1) { extern volatile int g_sleep_req; g_sleep_req = 1; return; }
    if (g_pbtn_action == 2) { power_off(); return; }
    if (g_pm_on && !pm_menu && g_ticks - pm_t > 30) { pm_do(0); return; }
    pm_open();
}
void pm_sleep_button(void) { { extern int g_sleeping; extern void power_wake_req(void); if (g_sleeping) { power_wake_req(); return; } } if (g_screen_off) { g_screen_off = 0; bl_screen(1); return; } { extern volatile int g_sleep_req; g_sleep_req = 1; } }

void pm_draw(void) {
    if (!g_pm_on) return;
    if (!pm_menu && g_ticks - pm_t > 3000) { g_pm_on = 0; return; }
    { extern int g_gm_ready; extern void gmetrics(void); if (!g_gm_ready) gmetrics(); }
    u32* sc = SCENE;
    if (g_gfx) gq_blend(g_rt, 0, 0, (int)W, (int)H, rgb(0, 0, 0), 140);
    else for (int j = 0; j < (int)H; j++) { u32* r = sc + (u64)j * SW; for (int i = 0; i < (int)W; i++) r[i] = mix(r[i], 0, 140); }
    int bw = 132, gap = 14; if (bw * pm_n + gap * (pm_n - 1) + 48 > (int)W - 16) bw = ((int)W - 16 - 48 - gap * (pm_n - 1)) / pm_n; int pw = bw * pm_n + gap * (pm_n - 1) + 48, ph = 190, px = ((int)W - pw) / 2, py = ((int)H - ph) / 2;
    u32 bg = rgb(32, 36, 48), fg = rgb(240, 244, 252), dim = rgb(150, 158, 178);
    fill_round(px + 3, py + 4, pw, ph, 18, 15, rgb(0, 0, 0)); fill_round(px, py, pw, ph, 18, 15, bg);
    pdstr_ctr(px + pw / 2, py + 22, "Power", fg, 1);
    pdstr_ctr(px + pw / 2, py + 46, pm_menu ? "What do you want to do?" : "Press the power button again to shut down", dim, 1);
    for (int k = 0; k < pm_n; k++) {
        int x = px + 24 + k * (bw + gap), y = py + 78, h = 84;
        int hov = in_rect(mx, my, x, y, bw, h), on = k == pm_sel, a = pm_act(k);
        u32 c = on ? ACC : hov ? rgb(58, 64, 82) : rgb(44, 49, 64);
        fill_round(x, y, bw, h, 12, 15, c);
        int cx = x + bw / 2, cy = y + 30;
        u32 ic = fg;
        if (a == 0) { fill_circle(cx, cy, 13, ic); fill_circle(cx, cy, 10, c); fill_rect(cx - 6, cy - 16, 12, 10, c); fill_rect(cx - 1, cy - 15, 3, 13, ic); }
        else if (a == 1) { fill_circle(cx, cy, 13, ic); fill_circle(cx, cy, 10, c); fill_rect(cx + 2, cy - 15, 14, 9, c); for (int t = 0; t < 6; t++) fill_rect(cx + 4 + t, cy - 14 + t, 7 - t, 1, ic); }
        else if (a == 2) { fill_circle(cx, cy, 12, ic); fill_circle(cx + 6, cy - 5, 11, c); }
        else if (a == 3) { fill_rect(cx - 12, cy - 11, 14, 22, ic); fill_rect(cx - 10, cy - 9, 10, 18, c); fill_rect(cx - 4, cy - 1, 16, 3, ic); for (int t = 0; t < 5; t++) fill_rect(cx + 7 + t, cy - 5 + t, 1, 11 - 2 * t, ic); }
        else if (a == 4) { fill_round(cx - 10, cy - 3, 20, 15, 3, 15, ic); fill_circle(cx, cy - 6, 7, ic); fill_circle(cx, cy - 6, 4, c); fill_rect(cx - 4, cy - 6, 8, 5, c); fill_rect(cx - 1, cy + 2, 3, 5, c); }
        else { for (int t = 0; t < 16; t++) { fill_rect(cx - 8 + t, cy - 8 + t, 2, 2, ic); fill_rect(cx + 7 - t, cy - 8 + t, 2, 2, ic); } }
        pdstr_ctr(cx, y + 56, PM_L[a], fg, 1);
        pm_hx[k] = x; pm_hy[k] = y; pm_hw[k] = bw; pm_hh[k] = h;
    }
}
int pm_mouse(int x, int y, int b) {
    int down = (b & 1) && !(pm_bprev & 1); pm_bprev = b;
    if (!down) { for (int k = 0; k < pm_n; k++) if (in_rect(x, y, pm_hx[k], pm_hy[k], pm_hw[k], pm_hh[k])) pm_sel = k; return 1; }
    for (int k = 0; k < pm_n; k++) if (in_rect(x, y, pm_hx[k], pm_hy[k], pm_hw[k], pm_hh[k])) { if (pm_act(k) == 5) pm_close(); else pm_do(pm_act(k)); return 1; }
    if (pm_menu) pm_close();
    return 1;
}
int pm_key(int c) {
    if (c == 27) { pm_close(); return 1; }
    if (c == 21 || c == 17) { pm_sel = (pm_sel + pm_n - 1) % pm_n; return 1; }
    if (c == 22 || c == 18 || c == '\t') { pm_sel = (pm_sel + 1) % pm_n; return 1; }
    if (c == '\n' || c == ' ') { if (pm_act(pm_sel) == 5) pm_close(); else pm_do(pm_act(pm_sel)); return 1; }
    return 1;
}

int pm_screen_off_draw(void) {
    if (!g_screen_off) { g_scr_drawn = 0; return 0; }
    if (!g_scr_drawn) { fill_rect(0, 0, (int)W, (int)H, rgb(0, 0, 0)); blit_all(); g_scr_drawn = 1; }
    return 1;
}
void pm_lid_changed(int open) {
    { extern int g_sleeping; extern void power_wake_req(void); if (g_sleeping) { if (open) power_wake_req(); return; } }
    if (!open) {
        g_pm_on = 0;
        { extern int g_state, g_logged_in; extern void acct_ui_lock(void); if (g_state == ST_DESKTOP && g_logged_in) acct_ui_lock(); }
        if (g_lid_action == 2) return;
        if (g_lid_action == 1) { extern volatile int g_sleep_req; g_sleep_req = 1; return; }
        g_screen_off = 1; bl_screen(0);
    } else if (g_screen_off) { g_screen_off = 0; bl_screen(1); }
}

static int g_bw_last = 101; static u64 g_bw_shut_at, g_bw_rep;
void pm_battery_tick(void) {
    if (!g_acb_n || g_acb_pct < 0) return;
    int dis = !g_acb_charging && g_ac_online != 1;
    if (!dis) { g_bw_last = 101; if (g_bw_shut_at) { g_bw_shut_at = 0; pm_toast("Charger connected - shutdown cancelled"); } return; }
    int p = g_acb_pct;
    if ((p <= 15 && g_bw_last > 15) || (p <= 7 && g_bw_last > 7)) { char t[80], *q = t; q = scopy(q, p <= 7 ? "Battery very low: " : "Battery low: "); q = udec(q, (u32)p); q = scopy(q, p <= 7 ? "% left - plug in the charger" : "% left - plug in the charger soon"); *q = 0; pm_toast(t); }
    if (p <= 3 && !g_bw_shut_at) { g_bw_shut_at = g_ticks + 6000; g_bw_rep = g_ticks; pm_toast("Battery at 3% - shutting down in 60 s to keep your files safe"); }
    if (g_bw_shut_at && g_ticks >= g_bw_shut_at) { g_bw_shut_at = 0; power_off(); }
    if (g_bw_shut_at && g_ticks - g_bw_rep >= 1500) { g_bw_rep = g_ticks; pm_toast("Battery critically low - shutting down soon, plug in the charger to cancel"); }
    g_bw_last = p;
}
int pm_shutdown_pending_s(void) { return g_bw_shut_at && g_bw_shut_at > g_ticks ? (int)((g_bw_shut_at - g_ticks) / 100) : -1; }
