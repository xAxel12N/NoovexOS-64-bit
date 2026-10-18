#ifndef NVX_FS_PARTS_H
#define NVX_FS_PARTS_H
#include "include/nvx/base.h"
#define BD_MAX 16
#define PT_MAX 256
typedef struct { int kind, unit, ratio; u64 sectors; char name[24]; char label[32]; } blkdev;
typedef struct { int disk, num, logical, scheme; u64 start, count; u8 mbrtype; char type[28]; char fs[12]; char name[40]; } partent;
extern blkdev g_bd[BD_MAX]; extern int g_bd_n;
extern partent g_pt[PT_MAX]; extern int g_pt_n;
int bd_read(int d,u64 lba,u8* buf);
int bd_scan(int usb);
int part_scan_disk(int d,partent* out,int cap);
int parts_scan_all(int usb);
int parts_scan_main(partent* out,int cap);
void parts_print(void);
#endif
