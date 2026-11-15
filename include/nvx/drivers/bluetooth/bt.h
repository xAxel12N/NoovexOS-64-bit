#ifndef NVX_DRIVERS_BLUETOOTH_BT_H
#define NVX_DRIVERS_BLUETOOTH_BT_H
#include "include/nvx/base.h"
int btusb_listed(u16* vid,u16* pid);
int btusb_probe(void);
int btusb_ok(void);
void btusb_ids(u16* vid,u16* pid);
void btusb_stats(u32* nev,u32* ndrop,u32* nerr,u32* ncmd);
int btusb_cmd(const u8* pkt,int n);
int btusb_evt(u8* out,int cap);
int btusb_bulk_out(const u8* p,int n,int ms);
int btusb_bulk_in(u8* out,int cap,int ms);
int btusb_vctrl(u8 rt,u8 req,u16 val,u16 idx,u8* buf,u16 len);
int bt_up(void);
int bt_scan(int secs);
int bt_status_text(char* out,int cap);
int bt_list_text(char* out,int cap);
#endif
