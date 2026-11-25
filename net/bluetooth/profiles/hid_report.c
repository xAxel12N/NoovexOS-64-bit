#include "include/nvx/base.h"
#include "include/nvx/drivers/input/input.h"
#include "net/bluetooth/bt_hid.h"
extern volatile unsigned char kdown[128];
extern volatile u64 g_ticks;
extern volatile u32 g_kbd_last_tick;
extern int kcaps, g_krep_delay, g_krep_rate, g_krep_flag;
void kpush(char c);
void hk_push(int id);
int usb_hid_key(u8 u,int shift);
u8 usb_hid_sc(u8 u);
static hidl_t* hidr_lay(hidr_t* r,int id){
    for(int i=0;i<r->nl;i++) if(r->l[i].id==id)return &r->l[i];
    if(r->nl>=HIDR_MAXL)return 0;
    hidl_t* L=&r->l[r->nl++]; short* z=&L->mod; for(int i=0;i<20;i++)z[i]=-1;
    L->id=(u8)id; L->kind=HIDK_NONE; L->arr_n=L->btn_n=L->bm_n=L->cc_n=0; return L;
}
int hidr_parse(hidr_t* r,const u8* d,int n){
    r->nl=0; r->hasid=0;
    u32 page=0, rsize=0, rcount=0, rid=0, us[16], umin=0, umax=0; int nu=0, haverange=0;
    u16 off[256]; for(int i=0;i<256;i++)off[i]=0;
    for(int i=0;i<n;){
        u8 b=d[i]; if(b==0xFE){ if(i+2>=n)break; i+=3+d[i+1]; continue; }
        int sz=b&3; if(sz==3)sz=4; int type=(b>>2)&3, tag=b>>4;
        if(i+1+sz>n)break;
        u32 v=0; for(int k=0;k<sz;k++)v|=(u32)d[i+1+k]<<(8*k);
        i+=1+sz;
        if(type==1){ if(tag==0)page=v; else if(tag==7)rsize=v; else if(tag==9)rcount=v; else if(tag==8){ rid=v&0xFF; r->hasid=1; } }
        else if(type==2){ u32 u=sz==4?v:(page<<16)|v;
            if(tag==0){ if(nu<16)us[nu++]=u; } else if(tag==1){ umin=u; haverange=1; } else if(tag==2)umax=u; }
        else if(type==0){
            if(tag==8&&rsize&&rcount&&rsize<=32&&rcount<=64){
                int bits=(int)(rsize*rcount), o=off[rid];
                if(!(v&1)){ hidl_t* L=hidr_lay(r,(int)rid);
                    u32 u0=nu?us[0]:haverange?umin:(page<<16); u32 pg=u0>>16, lo=u0&0xFFFF;
                    if(L&&pg==7){ L->kind=HIDK_KBD;
                        if(rsize==1&&haverange&&(umin&0xFFFF)>=0xE0){ if(L->mod<0)L->mod=(short)o; }
                        else if(rsize==1){ if(L->bm<0){ L->bm=(short)o; L->bm_n=(short)rcount; L->bm_min=(short)(umin&0xFFFF); } }
                        else if(L->arr<0){ L->arr=(short)o; L->arr_n=(short)rcount; L->arr_sz=(short)rsize; } }
                    else if(L&&pg==9){ if(L->btn<0){ L->btn=(short)o; L->btn_n=(short)rcount; } if(L->kind==HIDK_NONE)L->kind=HIDK_MOUSE; }
                    else if(L&&pg==1){ for(u32 k=0;k<rcount;k++){ u32 u=nu?(k<(u32)nu?us[k]:us[nu-1]):haverange?umin+k:0; int po=o+(int)(k*rsize);
                            if((u>>16)!=1)continue;
                            u&=0xFFFF;
                            if(u==0x30&&L->x<0){ L->x=(short)po; L->xs=(short)rsize; }
                            else if(u==0x31&&L->y<0){ L->y=(short)po; L->ys=(short)rsize; }
                            else if(u==0x38&&L->w<0){ L->w=(short)po; L->ws=(short)rsize; } }
                        if(L->x>=0&&L->kind!=HIDK_KBD)L->kind=HIDK_MOUSE; }
                    else if(L&&pg==0x0C){ if(lo==0x238&&rcount==1){ L->pan=(short)o; L->ps=(short)rsize; }
                        else if(L->cc<0){ L->cc=(short)o; L->cc_n=(short)rcount; L->cc_sz=(short)rsize; if(L->kind==HIDK_NONE)L->kind=HIDK_CC; } } }
                off[rid]=(u16)(o+bits); }
            nu=0; haverange=0; umin=umax=0;
        }
    }
    int k=0; for(int i=0;i<r->nl;i++) if(r->l[i].kind!=HIDK_NONE)r->l[k++]=r->l[i];
    r->nl=k; return k;
}
void hidr_boot(hidr_t* r,int kbd,int mouse){
    r->nl=0; r->hasid=1;
    if(kbd){ hidl_t* L=hidr_lay(r,1); L->kind=HIDK_KBD; L->mod=0; L->arr=16; L->arr_n=6; L->arr_sz=8; }
    if(mouse){ hidl_t* L=hidr_lay(r,2); L->kind=HIDK_MOUSE; L->btn=0; L->btn_n=5; L->x=8; L->xs=8; L->y=16; L->ys=8; L->w=24; L->ws=8; }
}
static int hb(const u8* p,int n,int off,int sz,int sg){
    if(off<0||sz<=0||sz>32||off+sz>n*8)return 0;
    u32 v=0; for(int i=0;i<sz;i++){ int b=off+i; if(p[b>>3]>>(b&7)&1)v|=1u<<i; }
    if(sg&&sz<32&&(v>>(sz-1)&1))v|=~0u<<sz;
    return (int)v;
}
static void hidr_mods(u8 m){ kdown[0x1D]=(m&0x11)?1:0; kdown[0x2A]=(m&0x22)?1:0; kdown[0x38]=(m&0x44)?1:0; kdown[0x5B]=(m&0x88)?1:0; }
static void hidr_kbd(hidr_t* r,const u8* d){
    u8* pv=r->kprev; u8 mod=d[0]; int shift=(mod&0x22)!=0;
    for(int i=2;i<8;i++) if(d[i]>=1&&d[i]<=3)return;
    hidr_mods(mod);
    for(int i=2;i<8;i++){ u8 k=d[i]; if(!k)continue; int was=0; for(int j=2;j<8;j++) if(pv[j]==k)was=1; if(was)continue;
        u8 sc=usb_hid_sc(k); if(sc)kdown[sc]=1; if(k>=4&&k<0xE0)g_kbd_last_tick=(u32)g_ticks;
        if(k==0x39){ kcaps^=1; continue; }
        int c=usb_hid_key(k,shift); if(c){ kpush((char)c); r->rep_c=c; r->rep_u=k; r->rep_t0=r->rep_t=g_ticks; } }
    for(int i=2;i<8;i++){ u8 k=pv[i]; if(!k)continue; int still=0; for(int j=2;j<8;j++) if(d[j]==k)still=1; if(still)continue;
        u8 sc=usb_hid_sc(k); if(sc)kdown[sc]=0; if(r->rep_u==k)r->rep_c=0; }
    for(int i=0;i<8;i++)pv[i]=d[i];
}
static int hidr_cc(int u){ switch(u){ case 0xE9: return 1; case 0xEA: return 2; case 0xE2: return 3; case 0xCD: return 4; case 0xB5: return 5; case 0xB6: return 6; case 0xB7: return 7; case 0x6F: return 13; case 0x70: return 14; } return 0; }
int hidr_input(hidr_t* r,int id,const u8* p,int n){
    if(!r->hasid)id=0;
    hidl_t* L=0; for(int i=0;i<r->nl;i++) if(r->l[i].id==id){ L=&r->l[i]; break; }
    if(!L||n<=0)return 0;
    if(L->kind==HIDK_MOUSE){
        int b=0; for(int i=0;i<L->btn_n&&i<8;i++) if(hb(p,n,L->btn+i,1,0))b|=1<<i;
        int dx=hb(p,n,L->x,L->xs,1), dy=hb(p,n,L->y,L->ys,1), wz=L->w>=0?hb(p,n,L->w,L->ws,1):0, pz=L->pan>=0?hb(p,n,L->pan,L->ps,1):0;
        if(dx||dy)in_rel(dx,dy);
        in_btn_raw(b&1,(b>>1)&1,(b>>2)&1); in_xbtn((b>>3)&1,(b>>4)&1); r->bprev=(u8)b;
        if(wz||pz)in_scroll(wz,pz);
        return HIDK_MOUSE; }
    if(L->kind==HIDK_KBD){
        u8 d[8]={0,0,0,0,0,0,0,0}; int k=2;
        if(L->mod>=0) for(int i=0;i<8;i++) if(hb(p,n,L->mod+i,1,0))d[0]|=(u8)(1<<i);
        if(L->arr>=0) for(int i=0;i<L->arr_n&&k<8;i++){ int u=hb(p,n,L->arr+i*L->arr_sz,L->arr_sz>8?8:L->arr_sz,0); if(u>=0xE0&&u<=0xE7)d[0]|=(u8)(1<<(u-0xE0)); else if(u)d[k++]=(u8)u; }
        if(L->bm>=0) for(int i=0;i<L->bm_n&&k<8;i++) if(hb(p,n,L->bm+i,1,0)){ int u=L->bm_min+i; if(u>=0xE0&&u<=0xE7)d[0]|=(u8)(1<<(u-0xE0)); else if(u>=4&&u<0x100)d[k++]=(u8)u; }
        hidr_kbd(r,d); return HIDK_KBD; }
    if(L->kind==HIDK_CC){ int now=0;
        for(int i=0;i<L->cc_n&&!now;i++)now=hb(p,n,L->cc+i*L->cc_sz,L->cc_sz,0);
        if(now&&now!=r->ccprev){ int h=hidr_cc(now); if(h)hk_push(h); }
        r->ccprev=(u16)now;
        return HIDK_CC; }
    return 0;
}
void hidr_release(hidr_t* r){
    u8 z[8]={0,0,0,0,0,0,0,0}; hidr_kbd(r,z); r->rep_c=0;
    if(r->bprev){ in_btn_raw(0,0,0); in_xbtn(0,0); r->bprev=0; }
}
void hidr_tick(hidr_t* r){
    if(!r->rep_c)return;
    if(g_ticks-r->rep_t0>=(u64)g_krep_delay&&g_ticks-r->rep_t>=(u64)g_krep_rate){ g_krep_flag=1; kpush((char)r->rep_c); g_krep_flag=0; r->rep_t=g_ticks; }
}
