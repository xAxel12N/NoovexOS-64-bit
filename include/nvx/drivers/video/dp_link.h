#ifndef NVX_DRIVERS_VIDEO_DP_LINK_H
#define NVX_DRIVERS_VIDEO_DP_LINK_H

#include "include/nvx/base.h"

#define DPCD_REV          0x000
#define DPCD_MAX_RATE     0x001
#define DPCD_MAX_LANES    0x002
#define DPCD_AUX_INTERVAL 0x00E
#define DPCD_LINK_BW      0x100
#define DPCD_LANE_SET     0x101
#define DPCD_TP_SET       0x102
#define DPCD_LANE0_SET    0x103
#define DPCD_LANE01_ST    0x202
#define DPCD_ALIGN_ST     0x204
#define DPCD_ADJ01        0x206
#define DPCD_POWER        0x600

#define DP_BW_RBR  0x06
#define DP_BW_HBR  0x0A
#define DP_BW_HBR2 0x14
#define DP_BW_HBR3 0x1E

#define DP_LT_OK      1
#define DP_LT_CR_FAIL 0
#define DP_LT_EQ_FAIL (-1)
#define DP_LT_IO      (-2)

typedef struct {
    int (*aux_rd)(void* c,u32 addr,u8* buf,int n);
    int (*aux_wr)(void* c,u32 addr,const u8* buf,int n);
    int (*set_link)(void* c,int lanes,int bw,int enh);
    int (*set_drive)(void* c,int lanes,const u8* vs,const u8* pe);
    int (*hw_train)(void* c,int lanes,int bw,int enh);
    void (*delay_us)(void* c,u32 us);
} dp_link_ops;

typedef struct {
    const dp_link_ops* o; void* c;
    u8 dpcd[16]; int src_bw, src_lanes, need_kbs;
    int max_bw, max_lanes, enh, tps3;
    int bw, lanes, ok, tries, cr_loops, eq_loops, aux_ops;
    u8 vs[4], pe[4], st[6];
    int kept, losses, retrains;
    const char* why;
} dp_link_t;

#define DP_LT_MAX_TRIES 12
#define DP_HPD_MAX_TRIES 3

int dp_bw_valid(int bw);
int dp_bw_lower(int bw);
u32 dp_link_kbs(int bw,int lanes);
int dp_lane_ok(const u8* st,int lanes,int eq);
int dp_link_caps(dp_link_t* l);
int dp_link_train_one(dp_link_t* l,int lanes,int bw);
int dp_link_train(dp_link_t* l);
int dp_link_off(dp_link_t* l);
int dp_link_read_cfg(dp_link_t* l);
int dp_link_status(dp_link_t* l);
int dp_link_inherit(dp_link_t* l);
int dp_link_recover(dp_link_t* l,int tries,u32 gap_us);

#endif
