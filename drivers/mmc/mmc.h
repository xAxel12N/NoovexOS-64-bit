#ifndef NVX_MMC_H
#define NVX_MMC_H
#include "include/nvx/base.h"

#define MMC_RSP_PRESENT 0x01
#define MMC_RSP_136     0x02
#define MMC_RSP_CRC     0x04
#define MMC_RSP_BUSY    0x08
#define MMC_RSP_IDX     0x10
#define MMC_RSP_NONE 0
#define MMC_RSP_R1  (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_IDX)
#define MMC_RSP_R1B (MMC_RSP_R1|MMC_RSP_BUSY)
#define MMC_RSP_R2  (MMC_RSP_PRESENT|MMC_RSP_136|MMC_RSP_CRC)
#define MMC_RSP_R3  (MMC_RSP_PRESENT)
#define MMC_RSP_R6  MMC_RSP_R1
#define MMC_RSP_R7  MMC_RSP_R1

#define MMC_DATA_READ  1
#define MMC_DATA_WRITE 2

#define MMC_VDD_33 0x00300000u
#define MMC_VDD_18 0x00000080u

#define MMC_BUS_1 1
#define MMC_BUS_4 4
#define MMC_BUS_8 8

#define MMC_E_OK      0
#define MMC_E_TIMEOUT (-1)
#define MMC_E_CRC     (-2)
#define MMC_E_IO      (-3)
#define MMC_E_NOCARD  (-4)
#define MMC_E_INVAL   (-5)

#define MMC_CAP_4BIT   0x01
#define MMC_CAP_8BIT   0x02
#define MMC_CAP_HS     0x04
#define MMC_CAP_DMA    0x08
#define MMC_CAP_NONREM 0x10

/* R2: resp[0]=bits 127..96 ... resp[3]=bits 31..0 of CID/CSD; short: resp[0]=card bits 39..8. Hosts wait out R1B busy; core sends CMD12 itself */
struct mmc_cmd { u32 opcode, arg, flags, resp[4]; u8* data; u32 blksz, blocks, dataflags; int error; };

struct mmc_host;
struct mmc_host_ops {
    int (*reset)(struct mmc_host*);
    int (*card_present)(struct mmc_host*);
    int (*set_power)(struct mmc_host*, u32 ocr_vdd);
    int (*set_clock)(struct mmc_host*, u32 hz);
    int (*set_width)(struct mmc_host*, int bits);
    int (*set_hs)(struct mmc_host*, int on);
    int (*send_cmd)(struct mmc_host*, struct mmc_cmd*);
};

#define MMC_TYPE_NONE 0
#define MMC_TYPE_SD   1
#define MMC_TYPE_MMC  2

struct mmc_card {
    int type, hc, width, ro;
    u32 rca, ocr, cid[4], csd[4], scr[2];
    u64 sectors;
    u32 max_hz;
    char name[8];
    u8 manf;
};

struct mmc_host {
    const struct mmc_host_ops* ops;
    void* priv;
    const char* name;
    u32 caps, ocr_avail, f_min, f_max, max_blocks;
    int present, ready;
    struct mmc_card card;
};

#define MMC_MAX_HOSTS 4
extern struct mmc_host* g_mmc_hosts[MMC_MAX_HOSTS];
extern int g_mmc_nhosts;

int mmc_register_host(struct mmc_host* h);
int mmc_card_init(struct mmc_host* h);
void mmc_card_remove(struct mmc_host* h);
int mmc_read_blocks(struct mmc_host* h, u64 lba, u32 n, void* buf);
int mmc_write_blocks(struct mmc_host* h, u64 lba, u32 n, const void* buf);
int mmc_poll_hosts(void);
const char* mmc_type_name(int t);
#endif
