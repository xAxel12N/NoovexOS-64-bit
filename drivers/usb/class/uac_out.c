#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/sysfeat.h"
#include "include/nvx/drivers/usb/uac.h"

static uac_ent_t* uac_ent(uac_topo_t* t,int id){ for(int i=0;i<t->nent;i++) if(t->ent[i].id==id)return &t->ent[i]; return 0; }
static uac_ent_t* uac_new(uac_topo_t* t,int id,int kind){ if(t->nent>=32||!id)return 0; uac_ent_t* e=&t->ent[t->nent++]; u8* z=(u8*)e; for(unsigned k=0;k<sizeof(*e);k++)z[k]=0; e->id=(u8)id; e->kind=(u8)kind; return e; }
static void uac_srcs(uac_ent_t* e,const u8* s,int nr,int room){ if(!e)return; for(int k=0;k<nr&&k<room&&e->nsrc<4;k++)e->src[e->nsrc++]=s[k]; }
static u32 uac_fu_bits(const u8* p,int sz,int v2){ u32 x=0; for(int k=0;k<sz&&k<4;k++)x|=(u32)p[k]<<(8*k); if(!v2)return x&3u; return (((x&3u)==3u)?1u:0u)|((((x>>2)&3u)==3u)?2u:0u); }
int uac_topo_parse(const u8* c,int n,uac_topo_t* t){
    u8* z=(u8*)t; for(unsigned k=0;k<sizeof(*t);k++)z[k]=0; t->ac_if=-1; t->ot=t->it=t->fu=t->clk=t->mic_it=-1;
    int cls=0,sub=0,ifn=-1,alt=0; uac_as_t* as=0;
    for(int i=0;i+2<=n;){ int bl=c[i]; if(bl<2||i+bl>n)break; int ty=c[i+1]; const u8* d=c+i;
        if(ty==4&&bl>=9){ ifn=d[2]; alt=d[3]; cls=d[5]; sub=d[6]; as=0;
            if(cls==1&&sub==1&&t->ac_if<0){ t->ac_if=ifn; t->v2=d[7]==0x20; }
            if(cls==1&&sub==2&&alt>0&&t->nas<16){ as=&t->as[t->nas++]; u8* q=(u8*)as; for(unsigned k=0;k<sizeof(*as);k++)q[k]=0; as->ifn=(u8)ifn; as->alt=(u8)alt; } }
        else if(ty==0x24&&cls==1&&sub==1&&ifn==t->ac_if&&bl>=4){ int st=d[2], v2=t->v2; uac_ent_t* e=0;
            if(st==2&&bl>=8){ e=uac_new(t,d[3],UAC_E_IT); if(e){ e->type=(u16)(d[4]|(d[5]<<8)); if(v2)e->clk=d[7]; } }
            else if(st==3&&bl>=9){ e=uac_new(t,d[3],UAC_E_OT); if(e){ e->type=(u16)(d[4]|(d[5]<<8)); uac_srcs(e,d+7,1,1); if(v2&&bl>=9)e->clk=d[8]; } }
            else if((st==4||st==5)&&bl>=6){ e=uac_new(t,d[3],UAC_E_UNIT); uac_srcs(e,d+5,d[4],bl-5); }
            else if(st==6&&bl>=6){ e=uac_new(t,d[3],UAC_E_FU); if(e){ uac_srcs(e,d+4,1,1); int sz=v2?4:d[5], o=v2?5:6;
                for(int ch=0;ch<3&&sz>0&&o+(ch+1)*sz<=bl;ch++)e->fc[ch]=uac_fu_bits(d+o+ch*sz,sz,v2); } }
            else if(!v2&&(st==7||st==8)&&bl>=8){ e=uac_new(t,d[3],UAC_E_UNIT); uac_srcs(e,d+7,d[6],bl-7); }
            else if(v2&&st==7&&bl>=7){ e=uac_new(t,d[3],UAC_E_UNIT); uac_srcs(e,d+6,1,1); }
            else if(v2&&(st==8||st==9)&&bl>=8){ e=uac_new(t,d[3],UAC_E_UNIT); uac_srcs(e,d+7,d[6],bl-7); }
            else if(v2&&st==0x0A&&bl>=5) uac_new(t,d[3],UAC_E_CLK);
            else if(v2&&st==0x0B&&bl>=6){ e=uac_new(t,d[3],UAC_E_CSEL); uac_srcs(e,d+5,d[4],bl-5); }
            else if(v2&&st==0x0C&&bl>=5){ e=uac_new(t,d[3],UAC_E_CMUL); uac_srcs(e,d+4,1,1); } }
        else if(ty==0x24&&as&&d[2]==1&&bl>=4) as->link=d[3];
        else if(ty==5&&as&&bl>=7&&(d[3]&3)==1){ int usage=(d[3]>>4)&3;
            if(!(d[2]&0x80)&&usage==0){ as->ep=d[2]; as->sync=(u8)((d[3]>>2)&3); if(bl>=9&&d[8])as->fb=d[8]; }
            else if((d[2]&0x80)&&(usage==1||(as->fb&&as->fb==d[2]))&&as->ep){ as->fb=d[2]; as->fbmps=(u16)(d[4]|(d[5]<<8)); as->fbint=d[6]; } }
        i+=bl; }
    return t->ac_if>=0; }
static int uac_trace(uac_topo_t* t,int id,int dep,int* fu){
    if(dep>16)return -1; uac_ent_t* e=uac_ent(t,id); if(!e)return -1;
    if(e->kind==UAC_E_IT)return e->type==0x0101?e->id:-1;
    for(int k=0;k<e->nsrc;k++){ int r=uac_trace(t,e->src[k],dep+1,fu); if(r>=0){ if(e->kind==UAC_E_FU&&(e->fc[0]|e->fc[1]|e->fc[2])&&*fu<0)*fu=e->id; return r; } }
    return -1; }
static int uac_ot_score(u16 ty){ if(ty==0x0302||ty==0x0402||ty==0x0403)return 5; if((ty>>8)==3)return 4; if((ty>>8)==6||(ty>>8)==4)return 3; if((ty>>8)==1)return 0; return 2; }
int uac_out_path(uac_topo_t* t){
    int best=-1, bs=0;
    for(int i=0;i<t->nent;i++){ uac_ent_t* e=&t->ent[i]; if(e->kind!=UAC_E_OT)continue; int s=uac_ot_score(e->type); if(s<=bs)continue;
        int fu=-1, it=e->nsrc?uac_trace(t,e->src[0],0,&fu):-1; if(it<0)continue; best=i; bs=s; t->ot=e->id; t->it=it; t->fu=fu; t->ot_type=e->type; }
    for(int i=0;i<t->nent;i++){ uac_ent_t* e=&t->ent[i]; if(e->kind==UAC_E_IT&&((e->type>>8)==2||e->type==0x0402||e->type==0x0403)){ t->mic_it=e->id; break; } }
    if(best<0)return 0;
    if(t->v2){ uac_ent_t* it=uac_ent(t,t->it); int ck=it&&it->clk?it->clk:t->ent[best].clk;
        for(int h=0;h<8&&ck;h++){ uac_ent_t* e=uac_ent(t,ck); if(!e)break; if(e->kind==UAC_E_CLK){ t->clk=ck; break; } if((e->kind!=UAC_E_CSEL&&e->kind!=UAC_E_CMUL)||!e->nsrc)break; ck=e->src[0]; } }
    return 1; }
const char* uac_kind_name(u16 ty){
    if(ty==0x0302)return "Headphones"; if(ty==0x0402||ty==0x0403)return "Headset"; if(ty==0x0301||ty==0x0304||ty==0x0305)return "Speakers";
    if(ty==0x0603)return "Line out"; if(ty==0x0602||ty==0x0605)return "Digital output"; if((ty>>8)==3)return "Speakers"; return "USB audio output"; }
int uac_as_find(const uac_topo_t* t,int ifn,int alt){ for(int i=0;i<t->nas;i++) if(t->as[i].ifn==ifn&&t->as[i].alt==alt)return i; return -1; }
u32 uac_pick(const u32* r,int n,int cont,u32 pref){
    if(cont&&n>=2){ u32 c[3]={pref,48000,44100}; for(int k=0;k<3;k++) if(c[k]&&c[k]>=r[0]&&c[k]<=r[1])return c[k]; return r[1]; }
    u32 c[3]={pref,48000,44100}; for(int j=0;j<3;j++) for(int k=0;k<n;k++) if(c[j]&&r[k]==c[j])return c[j];
    u32 best=0; for(int k=0;k<n;k++) if(r[k]<=48000&&r[k]>best)best=r[k]; if(!best&&n)best=r[0]; return best; }
static u32 uac_le32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
u32 uac2_range_pick(const u8* d,int n,u32 pref,u32 dflt){
    if(n<14)return dflt; int nr=d[0]|(d[1]<<8); u32 c[6]={pref,48000,44100,96000,88200,32000};
    for(int j=0;j<6;j++){ if(!c[j])continue; for(int k=0;k<nr&&2+k*12+12<=n;k++){ const u8* s=d+2+k*12; u32 mn=uac_le32(s), mx=uac_le32(s+4), rs=uac_le32(s+8);
        if(c[j]>=mn&&c[j]<=mx&&(!rs||(c[j]-mn)%rs==0))return c[j]; } }
    return dflt; }
void uac_rate_init(uac_rate_t* r,u32 rate,int hs){ r->hs=hs; r->nom=(u32)(((u64)rate<<16)/(hs?8000u:1000u)); r->fb=0; r->acc=0; r->n=0; r->bad=0; r->shift=0; r->locked=0; }
static int uac_near(u32 v,u32 nom){ return v>=nom-nom/8&&v<=nom+nom/8; }
int uac_fb_take(uac_rate_t* r,const u8* d,int n){
    if(n<3||!r->nom)return 0; u32 v=n>=4?uac_le32(d):((u32)d[0]|((u32)d[1]<<8)|((u32)d[2]<<16)); if(n==3)v<<=2;
    if(!r->locked){ static const int sh[9]={0,-1,1,-2,2,-3,3,-4,4};
        for(int k=0;k<9;k++){ u32 w=sh[k]>=0?v<<sh[k]:v>>(-sh[k]); if(w&&uac_near(w,r->nom)){ r->shift=sh[k]; r->locked=1; break; } }
        if(!r->locked){ r->bad++; return 0; } }
    u32 w=r->shift>=0?v<<r->shift:v>>(-r->shift); if(!uac_near(w,r->nom)){ r->bad++; return 0; }
    r->fb=w; r->n++; return 1; }
u32 uac_rate_frames(uac_rate_t* r,u32 ivus){ u32 per=r->hs?125u:1000u, u=ivus/per; if(!u)u=1; u64 q=(u64)(r->fb?r->fb:r->nom)*u+r->acc; r->acc=(u32)(q&0xFFFFu); return (u32)(q>>16); }
static int uac_ctl_range(const uac_vol_t* v,uac_req_fn req,int ch,short* mn,short* mx,short* rs){
    u16 val=(u16)((2<<8)|ch), ix=(u16)((v->fu<<8)|v->ac_if);
    if(v->v2){ u8 b[8]={0}; if(!req(0xA1,0x02,val,ix,8,1,b))return 0; if((b[0]|(b[1]<<8))<1)return 0; *mn=(short)(b[2]|(b[3]<<8)); *mx=(short)(b[4]|(b[5]<<8)); *rs=(short)(b[6]|(b[7]<<8)); return 1; }
    u8 a[2]={0},b[2]={0},c[2]={0}; if(!req(0xA1,0x82,val,ix,2,1,a)||!req(0xA1,0x83,val,ix,2,1,b))return 0; if(!req(0xA1,0x84,val,ix,2,1,c)){ c[0]=1; c[1]=0; }
    *mn=(short)(a[0]|(a[1]<<8)); *mx=(short)(b[0]|(b[1]<<8)); *rs=(short)(c[0]|(c[1]<<8)); return 1; }
int uac_vol_probe(const uac_topo_t* t,uac_req_fn req,uac_vol_t* v){
    u8* z=(u8*)v; for(unsigned k=0;k<sizeof(*v);k++)z[k]=0; if(t->fu<0||t->ac_if<0||!req)return 0;
    const uac_ent_t* e=0; for(int i=0;i<t->nent;i++) if(t->ent[i].id==t->fu)e=&t->ent[i]; if(!e)return 0;
    v->fu=t->fu; v->ac_if=t->ac_if; v->v2=t->v2; v->mute=(e->fc[0]&1)||(e->fc[1]&1);
    if(e->fc[0]&2)v->ch[v->nch++]=0; else for(int ch=1;ch<3;ch++) if(e->fc[ch]&2)v->ch[v->nch++]=(u8)ch;
    if(!v->nch)return v->mute;
    short mn,mx,rs; if(!uac_ctl_range(v,req,v->ch[0],&mn,&mx,&rs)||mx<=mn){ v->nch=0; return v->mute; }
    v->min=mn; v->max=mx; v->res=rs>0?rs:1; v->ok=1; return 1; }
int uac_vol_value(const uac_vol_t* v,int pct){
    if(pct<0)pct=0; if(pct>100)pct=100; int lo=v->min, hi=v->max; if(hi-lo>50*256)lo=hi-50*256; if(pct==0)return v->min;
    int x=lo+(hi-lo)*pct/100; int r=v->res>0?v->res:1; x=lo+((x-lo)/r)*r; if(x<v->min)x=v->min; if(x>v->max)x=v->max; return x; }
int uac_vol_set(const uac_vol_t* v,uac_req_fn req,int pct){
    int ok=1; u16 ix=(u16)((v->fu<<8)|v->ac_if);
    if(v->ok){ int x=uac_vol_value(v,pct); u8 b[2]={(u8)x,(u8)(x>>8)}; for(int k=0;k<v->nch;k++) if(!req(0x21,0x01,(u16)((2<<8)|v->ch[k]),ix,2,0,b))ok=0; }
    if(v->mute){ u8 m=pct<=0?1:0; if(!req(0x21,0x01,(u16)(1<<8),ix,1,0,&m))ok=0; }
    return ok&&(v->ok||v->mute); }
static uac_topo_t g_uactopo; static uac_vol_t g_uacvol; static uac_req_fn g_ureq; static uac_rate_t g_ufb; static int g_uon=0, g_ufbon=0, g_ubits=0, g_uch=0; static u32 g_urate=0;
u32 g_uac_rate_pref=48000;
static struct featguard uac_fbg={"USB audio feedback (asynchronous DACs)","feat-uacfb.cfg",1,0,3,0,0};
static struct featguard uac_vg={"USB audio hardware volume","feat-uacvol.cfg",1,0,3,0,0};
static char* uac_cp(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }
static char* uac_dec(char* p,u32 v){ char t[12]; int n=0; if(!v)t[n++]='0'; while(v){ t[n++]=(char)('0'+v%10); v/=10; } while(n)*p++=t[--n]; *p=0; return p; }
void uac_out_attach(const u8* c,int n,int hs,uac_req_fn req){
    g_uon=0; g_ufbon=0; g_urate=0; g_ureq=req; g_uacvol.ok=0; g_uacvol.mute=0; (void)hs;
    if(!uac_topo_parse(c,n,&g_uactopo)||!uac_out_path(&g_uactopo))return; g_uon=1;
    fg_boot(&uac_vg); if(fg_begin(&uac_vg)){ if(uac_vol_probe(&g_uactopo,req,&g_uacvol))fg_ok(&uac_vg); else if(g_uactopo.fu>=0)fg_fail(&uac_vg,"the device did not report its volume range"); else fg_ok(&uac_vg); }
    char b[120],*p=b; p=uac_cp(p,"usb audio out: "); p=uac_cp(p,uac_kind_name(g_uactopo.ot_type)); p=uac_cp(p,g_uactopo.v2?" (USB Audio 2)":" (USB Audio 1)");
    if(g_uacvol.ok){ p=uac_cp(p,", hardware volume on unit "); p=uac_dec(p,(u32)g_uactopo.fu); } if(g_uactopo.clk>=0){ p=uac_cp(p,", clock "); p=uac_dec(p,(u32)g_uactopo.clk); }
    if(g_uactopo.mic_it>=0)p=uac_cp(p,", with microphone"); klog(b); }
void uac_out_detach(void){ g_uon=0; g_ufbon=0; g_urate=0; g_uacvol.ok=0; g_uacvol.mute=0; }
int uac_out_present(void){ return g_uon; }
u32 uac_out_rate(const u32* r,int n,int cont,u32 dflt){ if(!n)return dflt; return uac_pick(r,n,cont,g_uac_rate_pref); }
u32 uac_out_rate2(const u8* d,int n,u32 dflt){ return uac2_range_pick(d,n,g_uac_rate_pref,dflt); }
int uac_out_clock(int dflt){ return g_uon&&g_uactopo.clk>=0?g_uactopo.clk:dflt; }
int uac_out_fb(int ifn,int alt,u8* ep,u16* mps,u8* intv){
    if(!g_uon)return 0; int k=uac_as_find(&g_uactopo,ifn,alt); if(k<0)return 0; uac_as_t* a=&g_uactopo.as[k];
    if(a->sync!=1||!(a->fb&0x80)||!a->fbmps)return 0; fg_boot(&uac_fbg); if(!fg_begin(&uac_fbg))return 0;
    *ep=a->fb; *mps=a->fbmps; *intv=a->fbint; return 1; }
void uac_out_fb_start(u32 rate,int hs){ uac_rate_init(&g_ufb,rate,hs); g_ufbon=1; }
void uac_out_fb_stop(void){ if(g_ufbon&&g_ufb.n)fg_ok(&uac_fbg); else if(g_ufbon)fg_fail(&uac_fbg,"the DAC sent no usable rate feedback"); g_ufbon=0; }
void uac_out_fb_data(const u8* d,int n){ if(!g_ufbon)return; if(uac_fb_take(&g_ufb,d,n)&&g_ufb.n==64)fg_ok(&uac_fbg); }
int uac_out_fb_live(void){ return g_ufbon; }
u32 uac_out_frames(u32 ivus){ return uac_rate_frames(&g_ufb,ivus); }
int uac_out_volume(int pct){ if(!g_uon||!g_ureq||(!g_uacvol.ok&&!g_uacvol.mute))return 0; if(!uac_vol_set(&g_uacvol,g_ureq,pct)){ g_uacvol.ok=0; g_uacvol.mute=0; fg_fail(&uac_vg,"setting the volume failed"); return 0; } return g_uacvol.ok; }
void uac_out_stream(u32 rate,int bits,int ch){ g_urate=rate; g_ubits=bits; g_uch=ch; }
const char* uac_out_desc(void){
    static char b[96]; char* p=b; if(!g_uon)return "No USB audio output";
    p=uac_cp(p,uac_kind_name(g_uactopo.ot_type));
    if(g_urate){ p=uac_cp(p,", "); p=uac_dec(p,(u32)g_ubits); p=uac_cp(p,"-bit "); p=uac_dec(p,g_urate/1000); if(g_urate%1000){ *p++='.'; p=uac_dec(p,(g_urate%1000)/100); } p=uac_cp(p," kHz"); }
    if(g_ufbon)p=uac_cp(p,g_ufb.n?", asynchronous":", waiting for rate feedback");
    p=uac_cp(p,g_uacvol.ok?", hardware volume":""); return b; }
u32 uac_out_rate_pref(void){ return g_uac_rate_pref; }
void uac_out_set_rate_pref(u32 r){ if(r==44100||r==48000||r==96000)g_uac_rate_pref=r; }
