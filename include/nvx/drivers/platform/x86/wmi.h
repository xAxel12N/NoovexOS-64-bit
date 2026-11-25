#ifndef NVX_DRIVERS_PLATFORM_X86_WMI_H
#define NVX_DRIVERS_PLATFORM_X86_WMI_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/acpi/aml.h"

enum { WMI_EXPENSIVE=1, WMI_METHOD=2, WMI_STRING=4, WMI_EVENT=8 };
typedef struct { u8 guid[16]; char oid[2]; u8 ninst, flags; anode* dev; } wmi_blk;
typedef void (*wmi_event_cb)(const wmi_blk* b,u32 nid,aobj* data);
#define WMI_MAX 48
#define WMI_DEV_MAX 8

extern wmi_blk g_wmi[WMI_MAX];
extern int g_wmi_n, g_wmi_ndev;
extern u32 g_wmi_events;

int wmi_guid_parse(const char* s,u8* g);
void wmi_guid_str(const u8* g,char* out);
const wmi_blk* wmi_find(const char* guid);
int wmi_has(const char* guid);
int wmi_method(const char* guid,u32 inst,u32 mid,const u8* in,u32 inlen,aobj* out);
int wmi_query(const char* guid,u32 inst,aobj* out);
int wmi_set_events(const char* guid,int on);
int wmi_on_event(const char* guid,wmi_event_cb cb);
int wmi_notify(anode* n,u32 v);
void wmi_init(void);
void hp_wmi_init(void);
int hp_wmi_notify(anode* n,u32 v);

#endif
