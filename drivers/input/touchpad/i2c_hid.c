#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/drivers/rtc/cmos.h"
#include "include/nvx/drivers/input/input.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/drivers/i2c/dw_i2c.h"
#include "include/nvx/drivers/input/touchpad/tp_hid.h"
#include "include/nvx/drivers/input/touchpad/tp_gesture.h"
#include "include/nvx/drivers/input/touchpad/i2c_hid.h"
#include "include/nvx/drivers/input/touchpad/elan_i2c.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"


tpdev g_tp;

void ih_delay_ms(int ms){ for(int i=0;i<ms;i++) for(int k=0;k<1000;k++) io_wait(); }
static int ih_read_reg(tpdev* t,u16 reg,u8* out,int n){ u8 w[2]={(u8)reg,(u8)(reg>>8)}; return dw_xfer(&t->bus,t->addr,w,2,out,n); }
int ih_get_desc(tpdev* t,u16 reg){
    u8 b[30]; for(int i=0;i<30;i++)b[i]=0; if(ih_read_reg(t,reg,b,30))return -1;
    ihdesc dd; ihdesc* d=&dd;
    d->len=(u16)(b[0]|(b[1]<<8)); d->bcd=(u16)(b[2]|(b[3]<<8)); d->rdlen=(u16)(b[4]|(b[5]<<8)); d->rdreg=(u16)(b[6]|(b[7]<<8)); d->inreg=(u16)(b[8]|(b[9]<<8)); d->maxin=(u16)(b[10]|(b[11]<<8));
    d->outreg=(u16)(b[12]|(b[13]<<8)); d->maxout=(u16)(b[14]|(b[15]<<8)); d->cmdreg=(u16)(b[16]|(b[17]<<8)); d->datareg=(u16)(b[18]|(b[19]<<8)); d->vid=(u16)(b[20]|(b[21]<<8)); d->pid=(u16)(b[22]|(b[23]<<8)); d->ver=(u16)(b[24]|(b[25]<<8));
    if(d->len!=30||d->bcd!=0x0100||!d->rdlen||d->rdlen>4096||d->maxin<3||d->maxin>512)return -2;
    t->d=dd; t->descreg=reg; return 0; }
static int ih_cmd(tpdev* t,u8 op,u8 low){ u8 w[4]={(u8)t->d.cmdreg,(u8)(t->d.cmdreg>>8),low,op}; return dw_xfer(&t->bus,t->addr,w,4,0,0); }
int ih_read_input(tpdev* t,u8* out,int n){ return dw_xfer(&t->bus,t->addr,0,0,out,n); }
static int ih_reset(tpdev* t){
    if(ih_cmd(t,0x01,0))return -1;
    for(int i=0;i<200;i++){ ih_delay_ms(5); u8 b[2]={0xFF,0xFF}; if(ih_read_input(t,b,2)==0&&b[0]==0&&b[1]==0)return 0; }
    return 1; }
static int ih_set_feature(tpdev* t,int rid,const u8* data,int dlen){
    u8 w[80]; int n=0; if(dlen>60)return -1;
    w[n++]=(u8)t->d.cmdreg; w[n++]=(u8)(t->d.cmdreg>>8); w[n++]=(u8)(0x30|(rid<15?rid:15)); w[n++]=0x03; if(rid>=15)w[n++]=(u8)rid;
    w[n++]=(u8)t->d.datareg; w[n++]=(u8)(t->d.datareg>>8);
    int sz=2+(t->lay.uses_ids?1:0)+dlen; w[n++]=(u8)sz; w[n++]=(u8)(sz>>8); if(t->lay.uses_ids)w[n++]=(u8)rid;
    for(int i=0;i<dlen;i++)w[n++]=data[i];
    return dw_xfer(&t->bus,t->addr,w,n,0,0); }
static int ih_get_feature(tpdev* t,int rid,u8* out,int dlen){
    u8 w[8]; int n=0; w[n++]=(u8)t->d.cmdreg; w[n++]=(u8)(t->d.cmdreg>>8); w[n++]=(u8)(0x30|(rid<15?rid:15)); w[n++]=0x02; if(rid>=15)w[n++]=(u8)rid;
    w[n++]=(u8)t->d.datareg; w[n++]=(u8)(t->d.datareg>>8);
    u8 r[72]; int want=2+(t->lay.uses_ids?1:0)+dlen; if(want>72)return -1;
    if(dw_xfer(&t->bus,t->addr,w,n,r,want))return -1;
    int off=2+(t->lay.uses_ids?1:0); for(int i=0;i<dlen;i++)out[i]=r[off+i]; return 0; }
static int ih_enable_ptp(tpdev* t){
    tplay* L=&t->lay; if(!L->mode.ok)return -1;
    int ml=L->feat_len[L->mode_rid]; if(ml<1||ml>60)return -1;
    u8 b[64]; for(int i=0;i<64;i++)b[i]=0; tp_setbits(b,ml,L->mode,3);
    if(ih_set_feature(t,L->mode_rid,b,ml))return -2;
    if(L->surf.ok||L->bsw.ok){ int sl=L->feat_len[L->sw_rid]; if(sl>=1&&sl<=60){ for(int i=0;i<64;i++)b[i]=0; if(L->surf.ok)tp_setbits(b,sl,L->surf,1); if(L->bsw.ok)tp_setbits(b,sl,L->bsw,1); ih_set_feature(t,L->sw_rid,b,sl); } }
    return 0; }
static u8* g_ih_rd2;
int g_ih_rd_tries, g_ih_rd_same, g_ih_rd_slow;
static int ih_read_rdesc(tpdev* t,int slow){
    if(slow){ if(dw_init(&t->bus,t->bus.base,100000u))return -1; g_ih_rd_slow=1; }
    if(!g_ih_rd2)g_ih_rd2=(u8*)kmalloc(4096);
    int n=t->d.rdlen, got=0;
    for(int a=0;a<4;a++){
        g_ih_rd_tries++;
        if(ih_read_reg(t,t->d.rdreg,t->rdesc,n)){ ih_delay_ms(5); continue; } got=1;
        if(!g_ih_rd2)return 0;
        if(ih_read_reg(t,t->d.rdreg,g_ih_rd2,n)){ ih_delay_ms(5); continue; }
        int same=1; for(int i=0;i<n;i++) if(t->rdesc[i]!=g_ih_rd2[i]){ same=0; break; }
        if(same){ g_ih_rd_same=1; return 0; }
        ih_delay_ms(5); }
    return got?1:-1; }
static int ih_layout_ok(tpdev* t){ return t->lay.ptp||t->lay.mouse; }
int ih_setup(tpdev* t){
    t->stage=1; ih_cmd(t,0x08,0); ih_delay_ms(2);
    t->stage=2; ih_reset(t);
    t->stage=3; if(!t->rdesc)t->rdesc=(u8*)kmalloc(4096); if(!t->rdesc)return -1;
    g_ih_rd_tries=0; g_ih_rd_same=0; g_ih_rd_slow=0;
    int rr=ih_read_rdesc(t,0); if(rr<0)rr=ih_read_rdesc(t,1); if(rr<0)return -2;
    t->stage=4; int pr=tp_parse(t->rdesc,t->d.rdlen,&t->lay);
    if((pr||!ih_layout_ok(t)||rr)&&!g_ih_rd_slow){ if(ih_read_rdesc(t,1)>=0){ pr=tp_parse(t->rdesc,t->d.rdlen,&t->lay); } }
    if(pr)return -3;
    if(t->lay.ptp){ t->stage=5; if(ih_enable_ptp(t)==0){ t->mode=1; tpe_init(&t->eng,t->lay.xmin,t->lay.xmax,t->lay.ymin,t->lay.ymax,t->lay.xum,t->lay.yum); } }
    if(!t->mode&&t->lay.mouse)t->mode=2;
    if(!t->mode)return -4;
    t->stage=6; t->ok=1; return 0; }
static void ih_apply(tpdev* t,const tpout* o){
    in_rel(o->dx,o->dy); if(o->btn_changed)in_buttons(o->btn_l,o->btn_r,o->btn_m); in_scroll(o->scroll_v,o->scroll_h); if(o->gesture){ in_gesture(o->gesture); t->gest_last=o->gesture; } }
static u32 ih_ms(void){ return (u32)(hr_ns()/1000000ULL); }
void ih_frame(tpdev* t,const tpcontact* c,int n,int btn){
    tpout o; t->ncont=n; for(int i=0;i<n;i++)t->cont[i]=c[i]; t->btn=btn; t->frames++;
    tpe_frame(&t->eng,c,n,btn,ih_ms(),&o); t->eng.palm=0; ih_apply(t,&o); }
static void ih_flush(tpdev* t){ if(t->pend_need<=0)return; t->eng.palm=t->pend_palm; ih_frame(t,t->pend,t->pend_n,t->pend_btn); t->pend_need=0; t->pend_got=0; t->pend_n=0; t->pend_palm=0; }
static void ih_process(tpdev* t,const u8* b,int n){
    if(n<2)return; if(t->proto==1){ el_process(t,b,n); return; } int len=b[0]|(b[1]<<8); if(len==0){ t->empty++; return; } if(len<3||len>n||len>t->d.maxin)return;
    const u8* p=b+2; int pl=len-2; int rid=0; if(t->lay.uses_ids){ rid=p[0]; p++; pl--; }
    tplay* L=&t->lay;
    if(t->mode==1&&rid==L->ptp_rid){
        tpcontact cs[TPF_MAX]; int cnt=0, palm=0;
        for(int k=0;k<L->nfing;k++){ int tip=tp_bits(p,pl,L->f[k].tip); if(!tip)continue; int conf=L->f[k].conf.ok?tp_bits(p,pl,L->f[k].conf):1; if(!conf){ palm++; continue; }
            cs[cnt].id=L->f[k].cid.ok?tp_bits(p,pl,L->f[k].cid):k; cs[cnt].x=tp_bits(p,pl,L->f[k].x); cs[cnt].y=tp_bits(p,pl,L->f[k].y); cnt++; }
        int cc=L->cc.ok?tp_bits(p,pl,L->cc):-1; int btn=L->btn[0].ok?tp_bits(p,pl,L->btn[0]):0;
        if(cc!=0){
            if(t->pend_need>0){ t->frames_lost++; ih_flush(t); }
            int nf=L->nfing>0?L->nfing:1; int need=(cc>nf&&cc<=TPF_MAX)?(cc+nf-1)/nf:1;
            if(need<=1){ t->eng.palm=palm; ih_frame(t,cs,cnt,btn); return; }
            t->pend_n=0; for(int i=0;i<cnt;i++)t->pend[t->pend_n++]=cs[i]; t->pend_need=need; t->pend_got=1; t->pend_btn=btn; t->pend_palm=palm; t->frames_split++; return; }
        if(t->pend_need>0){ for(int i=0;i<cnt&&t->pend_n<TPF_MAX;i++)t->pend[t->pend_n++]=cs[i]; t->pend_got++; t->pend_btn|=btn; t->pend_palm+=palm; if(t->pend_got>=t->pend_need)ih_flush(t); return; }
        t->eng.palm=palm; ih_frame(t,cs,cnt,btn); return; }
    if(t->mode>=1&&L->mouse&&rid==L->m_rid){
        int l=tp_bits(p,pl,L->mbtn[0]), r=tp_bits(p,pl,L->mbtn[1]), m=tp_bits(p,pl,L->mbtn[2]);
        in_rel(tp_bits(p,pl,L->mx),tp_bits(p,pl,L->my)); in_buttons(l,r,m); in_scroll(tp_bits(p,pl,L->mw),0); t->frames++; } }
void igp_sample(void); void igp_learn(int rep); int igp_ready(void); int igp_asserted(void);
static int ih_gpio_asserted(tpdev* t){ u32 v=*(volatile u32*)(t->gpio_base+(u64)t->pin*4u); int lvl=(v>>16)&1; return t->active_low?!lvl:lvl; }
void tp_poll(void){
    tpdev* t=&g_tp; if(!t->ok)return;
    { extern int g_lap_tp_off; if(g_lap_tp_off)return; }
    if(t->eng.pulse){ tpout o; tpe_frame(&t->eng,t->cont,0,t->btn,ih_ms(),&o); ih_apply(t,&o); }
    { u64 now=g_ticks; if(!t->rate_t0)t->rate_t0=now; if(now-t->rate_t0>=100){ t->rate=t->rate_cnt*100u/(u32)(now-t->rate_t0); t->rate_cnt=0; t->rate_t0=now; } }
    int pin=t->gpio_ok||igp_ready();
    for(int round=0;round<6;round++){
        int lrn=0;
        if(t->gpio_ok){ if(!ih_gpio_asserted(t))return; }
        else if(igp_ready()){ if(!igp_asserted())return; }
        else { if(round==0){ if(g_ticks==t->last_tick)return; t->last_tick=g_ticks; igp_sample(); lrn=1; } else if(round>=4)return; }
        int n=t->d.maxin; if(n>512)n=512;
        if(ih_read_input(t,t->buf,n))return; t->reads++;
        int len=t->buf[0]|(t->buf[1]<<8);
        if(lrn)igp_learn(len>2&&len<=n);
        if(len<=2){ t->empty++; if(!pin)return; continue; }
        if(!pin&&len==t->lastn){ int same=1; for(int i=0;i<len&&i<n;i++) if(t->buf[i]!=t->last[i]){ same=0; break; } if(same){ t->dup++; return; } }
        if(len<=n){ for(int i=0;i<len;i++)t->last[i]=t->buf[i]; t->lastn=len; }
        t->rate_cnt++;
        ih_process(t,t->buf,n); } }
