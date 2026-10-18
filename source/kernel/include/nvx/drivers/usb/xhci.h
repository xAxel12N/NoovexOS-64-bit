#ifndef NVX_DRIVERS_USB_XHCI_H
#define NVX_DRIVERS_USB_XHCI_H

#include "include/nvx/base.h"

#define XHC_MAX 4
#define XMR(a)   (*(volatile u32*)(u64)(a))
#define XHID_MAX 10
#define XHUB_MAX 6
typedef struct { int used, ctl, reg, slot, root, depth, speed, nports, ttt, tt_slot, tt_port, parent, pport, ss; u32 route, e0_enq, e0_cyc, conn; } xhub_t;
typedef struct { u8 id, kind; short btn_off, btn_cnt, x_off, x_sz, y_off, y_sz, w_off, w_sz, mod_off, arr_off, arr_cnt, bm_off, bm_min, bm_cnt; } hidlay_t;
#define HIDLAY_MAX 4
struct nvx_ufs_t { char name[16]; u32 size, clus; };

extern int g_xcur , g_xhc_n , msd_ctl , xhci_pci_fn , xhci_hi4g;
extern int g_xspb_n[XHC_MAX], g_xspb_need[XHC_MAX];
extern int xhci_present , xhci_init_ok , xhci_slots , xhci_ports;
extern u64 xhci_base , xhci_op;
extern int xhci_ports_hw , xhci_pci_bus , xhci_pci_slot;
extern int g_cursor_moved;
extern u32 cur_ep0 , cur_dbuf , e0_enq , e0_cyc;
extern int xhid_n , xhci_mouse_ok;
extern int xhid_used[XHID_MAX], xhid_slot[XHID_MAX], xhid_dci[XHID_MAX], xhid_rlen[XHID_MAX], xhid_iskbd[XHID_MAX], xhid_port[XHID_MAX];
extern int xhid_nlay[XHID_MAX], xhid_rep[XHID_MAX], xhid_hasid[XHID_MAX], xhid_ctl[XHID_MAX];
extern u32 xhid_cnt[XHID_MAX];
extern u8 xhid_last[XHID_MAX][16];
extern int xhid_lastn[XHID_MAX];
extern xhub_t xhub[XHUB_MAX];
extern u32 xhid_route[XHID_MAX];
extern int xhid_depth[XHID_MAX];
extern hidlay_t xhid_lay[XHID_MAX][HIDLAY_MAX];
extern char xhci_failstep[48];
extern int xhci_failcc;
extern int msd_ok , msd_slot , msd_in_dci , msd_out_dci;
extern u32 msd_lastlba , msd_bsize;
extern int ufat_ok , ufat_type , ufat_nfats;
extern struct nvx_ufs_t ufs[32];
extern int ufs_n;
extern int g_files_usb , g_usb_arrived , msd_port;

void xhci_mouse_poll(void);
void xhci_mouse_autoinit(void);
int msd_autoinit(void);
void ufat_mount(void);
int ufat_read(int fi,char* out,int max);
int ufat_write_file(const char* name,const char* data,int len);
int ufat_delete_file(int idx);
void xhc_select(int i);
void usb_hotplug_poll(void);

#define USBDEV_MAX 16
typedef struct { int used, ctl, port, cam, prn, hid; u16 vid, pid; char kind[32]; char name[40]; u64 t; } usbdev_t;
extern usbdev_t g_usbdev[USBDEV_MAX]; extern int g_usbdev_n; extern u32 g_usbdev_gen;
void usbdev_note(int port,u16 vid,u16 pid,const u8* cfg,int n,const char* name);
void usbdev_gone(int port);
#endif
