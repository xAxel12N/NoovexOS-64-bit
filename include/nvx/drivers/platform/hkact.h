#ifndef NVX_DRIVERS_PLATFORM_HKACT_H
#define NVX_DRIVERS_PLATFORM_HKACT_H
enum { HKA_NONE=0, HKA_BRI_UP, HKA_BRI_DN, HKA_VOL_UP, HKA_VOL_DN, HKA_MUTE, HKA_MICMUTE, HKA_AIRPLANE, HKA_WIFI, HKA_BT,
    HKA_KBD_UP, HKA_KBD_DN, HKA_KBD_CYCLE, HKA_TOUCHPAD, HKA_DISPLAY, HKA_SLEEP, HKA_LOCK, HKA_CALC, HKA_PLAY, HKA_NEXT, HKA_PREV,
    HKA_STOP, HKA_CAMERA, HKA_SCREENSHOT, HKA_BROWSER, HKA_FILES, HKA_MAX };
void hkact(int a);
int hkact_bri_step(int dir);
int hkact_bri_get(void);
void hkact_airplane(int on);
void hkact_wifi(int on);
void hkact_bt(int on);
void hkact_kbd_register(int max,int cur,void (*set)(int));
void hkact_kbd_report(int lvl);
void hkact_kbd_step(int dir);
int hkact_kbd_set_cb(int lvl);
extern int g_hka_osd, g_hka_events, g_hka_last, g_hka_bt_off;
#endif
