#include "include/nvx/base.h"
#include "include/nvx/drivers/video/dp_link.h"

int dp_bw_valid(int bw){ return bw==DP_BW_RBR||bw==DP_BW_HBR||bw==DP_BW_HBR2||bw==DP_BW_HBR3; }
int dp_bw_lower(int bw){ return bw>DP_BW_HBR2?DP_BW_HBR2:bw>DP_BW_HBR?DP_BW_HBR:bw>DP_BW_RBR?DP_BW_RBR:0; }
u32 dp_link_kbs(int bw,int lanes){ return (u32)bw*27000u*(u32)lanes; }
int dp_lane_ok(const u8* st,int lanes,int eq){
    u8 m=eq?7:1; for(int i=0;i<lanes;i++) if(((st[i>>1]>>((i&1)*4))&m)!=m)return 0;
    return eq?(st[2]&1):1; }
static int rd(dp_link_t* l,u32 a,u8* b,int n){ l->aux_ops++; return l->o->aux_rd(l->c,a,b,n)==n; }
static int wr(dp_link_t* l,u32 a,const u8* b,int n){ l->aux_ops++; return l->o->aux_wr(l->c,a,b,n)==n; }
static int wr1(dp_link_t* l,u32 a,u8 v){ return wr(l,a,&v,1); }
static void wait_us(dp_link_t* l,u32 us){ if(l->o->delay_us)l->o->delay_us(l->c,us); }
static u32 interval(dp_link_t* l,int eq){ u32 v=l->dpcd[DPCD_AUX_INTERVAL]&0x7F; if(!v)return eq?400:100; if(v>4)v=4; return v*4000u; }
int dp_link_caps(dp_link_t* l){
    l->why=0; for(int i=0;i<16;i++)l->dpcd[i]=0;
    if(!rd(l,DPCD_REV,l->dpcd,16)){ l->why="DPCD read over AUX failed"; return 0; }
    if(!l->dpcd[DPCD_REV]){ l->why="DPCD revision is 0 (no DisplayPort sink)"; return 0; }
    int bw=l->dpcd[DPCD_MAX_RATE]; while(bw&&!dp_bw_valid(bw))bw=dp_bw_lower(bw);
    if(l->src_bw&&bw>l->src_bw){ bw=l->src_bw; while(bw&&!dp_bw_valid(bw))bw=dp_bw_lower(bw); }
    int ln=l->dpcd[DPCD_MAX_LANES]&0x1F; ln=ln>=4?4:ln>=2?2:ln;
    if(l->src_lanes&&ln>l->src_lanes)ln=l->src_lanes>=4?4:l->src_lanes>=2?2:1;
    l->max_bw=bw; l->max_lanes=ln; l->enh=(l->dpcd[DPCD_MAX_LANES]>>7)&1; l->tps3=(l->dpcd[DPCD_MAX_LANES]>>6)&1;
    if(!bw||!ln){ l->why="sink reports no usable link rate or lane count"; return 0; }
    return 1; }
static int drive(dp_link_t* l,u8 tp){
    u8 s[4]; for(int i=0;i<l->lanes;i++)s[i]=(u8)(l->vs[i]|(l->vs[i]==3?4:0)|(l->pe[i]<<3)|(l->pe[i]==3?0x20:0));
    if(l->o->set_drive&&!l->o->set_drive(l->c,l->lanes,l->vs,l->pe))return 0;
    if(tp&&!wr1(l,DPCD_TP_SET,tp))return 0;
    return wr(l,DPCD_LANE0_SET,s,l->lanes); }
static int adjust(dp_link_t* l){
    int ch=0; for(int i=0;i<l->lanes;i++){ u8 b=l->st[4+(i>>1)]>>((i&1)*4); u8 v=b&3, p=(b>>2)&3; if(v+p>3)p=(u8)(3-v); if(v!=l->vs[i]||p!=l->pe[i])ch=1; l->vs[i]=v; l->pe[i]=p; }
    return ch; }
static int status(dp_link_t* l,int eq){ wait_us(l,interval(l,eq)); return rd(l,DPCD_LANE01_ST,l->st,6); }
static int train_cr(dp_link_t* l){
    int same=0;
    for(int k=0;k<10;k++){ l->cr_loops++;
        if(!status(l,0))return DP_LT_IO;
        if(dp_lane_ok(l->st,l->lanes,0))return DP_LT_OK;
        int mx=1; for(int i=0;i<l->lanes;i++) if(l->vs[i]!=3)mx=0;
        if(mx){ l->why="clock recovery failed at maximum voltage swing"; return DP_LT_CR_FAIL; }
        u8 v0=l->vs[0]; adjust(l); if(l->vs[0]==v0){ if(++same>=5){ l->why="clock recovery failed: same voltage tried 5 times"; return DP_LT_CR_FAIL; } } else same=0;
        if(!drive(l,0))return DP_LT_IO; }
    l->why="clock recovery did not finish in 10 steps"; return DP_LT_CR_FAIL; }
static int train_eq(dp_link_t* l){
    if(!drive(l,(u8)(l->tps3?0x23:0x22)))return DP_LT_IO;
    for(int k=0;k<6;k++){ l->eq_loops++;
        if(!status(l,1))return DP_LT_IO;
        if(!dp_lane_ok(l->st,l->lanes,0)){ l->why="clock recovery lost during channel equalisation"; return DP_LT_CR_FAIL; }
        if(dp_lane_ok(l->st,l->lanes,1))return DP_LT_OK;
        adjust(l); if(!drive(l,0))return DP_LT_IO; }
    l->why="channel equalisation did not finish in 6 steps"; return DP_LT_EQ_FAIL; }
int dp_link_off(dp_link_t* l){ return wr1(l,DPCD_TP_SET,0); }
int dp_link_train_one(dp_link_t* l,int lanes,int bw){
    l->lanes=lanes; l->bw=bw; l->ok=0; l->tries++; for(int i=0;i<4;i++){ l->vs[i]=0; l->pe[i]=0; }
    u8 cfg[2]={(u8)bw,(u8)(lanes|(l->enh?0x80:0))};
    if(l->o->hw_train){ int r=l->o->hw_train(l->c,lanes,bw,l->enh);
        if(r<0){ l->why="link training request failed"; return DP_LT_IO; }
        if(!rd(l,DPCD_LANE01_ST,l->st,6))return DP_LT_IO;
        if(!dp_lane_ok(l->st,lanes,0)){ l->why="clock recovery failed (firmware training)"; return DP_LT_CR_FAIL; }
        if(!r||!dp_lane_ok(l->st,lanes,1)){ l->why="channel equalisation failed (firmware training)"; return DP_LT_EQ_FAIL; }
        l->ok=1; return DP_LT_OK; }
    if(l->o->set_link&&!l->o->set_link(l->c,lanes,bw,l->enh)){ l->why="source refused the link configuration"; return DP_LT_IO; }
    if(!wr(l,DPCD_LINK_BW,cfg,2))return DP_LT_IO;
    if(!drive(l,0x21))return DP_LT_IO;
    int r=train_cr(l); if(r==DP_LT_OK)r=train_eq(l);
    if(!dp_link_off(l)&&r==DP_LT_OK)r=DP_LT_IO;
    l->ok=r==DP_LT_OK; return r; }
int dp_link_train(dp_link_t* l){
    l->tries=0; l->cr_loops=0; l->eq_loops=0; l->ok=0;
    if(!l->max_bw&&!dp_link_caps(l))return 0;
    wr1(l,DPCD_POWER,1);
    int lanes=l->max_lanes, bw=l->max_bw;
    while(lanes&&l->tries<DP_LT_MAX_TRIES){
        if(l->need_kbs&&dp_link_kbs(bw,lanes)<(u32)l->need_kbs){ l->why="not enough link bandwidth left for the mode"; break; }
        int r=dp_link_train_one(l,lanes,bw);
        if(r==DP_LT_OK){ l->why=0; return 1; }
        if(r==DP_LT_IO){ if(!l->why)l->why="AUX transfer failed during link training"; break; }
        int lb=dp_bw_lower(bw);
        if(lb)bw=lb; else { lanes=lanes>2?2:lanes>1?1:0; bw=l->max_bw; } }
    if(!l->why)l->why="link training failed at every rate and lane count";
    l->ok=0; return 0; }
int dp_link_read_cfg(dp_link_t* l){ u8 c[2]; if(!rd(l,DPCD_LINK_BW,c,2))return 0; int ln=c[1]&0x1F;
    if(!dp_bw_valid(c[0])||!(ln==1||ln==2||ln==4)){ return 0; } l->bw=c[0]; l->lanes=ln; l->enh=(c[1]>>7)&1; return 1; }
int dp_link_status(dp_link_t* l){ if(!l->lanes)return 0; if(!rd(l,DPCD_LANE01_ST,l->st,6))return -1; return dp_lane_ok(l->st,l->lanes,1); }
int dp_link_inherit(dp_link_t* l){
    l->ok=0; l->kept=0; if(!l->max_bw&&!dp_link_caps(l))return 0;
    if(!dp_link_read_cfg(l)){ l->why="the sink has no trained link configuration"; l->lanes=0; return 0; }
    if(l->need_kbs&&dp_link_kbs(l->bw,l->lanes)<(u32)l->need_kbs){ l->why="the firmware link is too slow for the native mode"; return 0; }
    int s=dp_link_status(l); if(s<0){ l->why="AUX read of the link status failed"; return 0; }
    if(!s){ l->losses++; l->why="the firmware link has lost clock recovery or equalisation"; return 0; }
    l->ok=1; l->kept=1; l->why=0; return 1; }
int dp_link_recover(dp_link_t* l,int tries,u32 gap_us){
    int s=dp_link_status(l); if(s>0){ l->ok=1; return 1; }
    l->losses++; l->ok=0; l->kept=0; if(tries>DP_HPD_MAX_TRIES)tries=DP_HPD_MAX_TRIES;
    for(int t=0;t<tries;t++){ if(t)wait_us(l,gap_us); l->retrains++; l->max_bw=0; if(dp_link_train(l))return 1; }
    if(!l->why){ l->why="the link could not be trained again"; } return 0; }
