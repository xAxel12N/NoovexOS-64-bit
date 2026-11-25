#include "drivers/video/amd/amd.h"
#include "drivers/video/amd/dc/dcn_state.h"
#include "include/nvx/lib/base/fmt.h"

amd_dc_t g_amd_dc;
#define DR(o) amd_rreg(g_amd_dc.seg2+(o))
#define DW(o,v) amd_wreg(g_amd_dc.seg2+(o),(v))
enum { R_OTG_HT=0x1b2a, R_OTG_HB=0x1b2b, R_OTG_VT=0x1b2f, R_OTG_VB=0x1b36, R_OTG_CTL=0x1b41, OTG_STR=0x80, R_ODM_SRC=0x1acb, ODM_STR=0x10,
       R_HUBP_CFG=0x05e5, R_HUBP_VP=0x05ea, R_HUBP_CNTL=0x05f3, R_HUBP_PITCH=0x0607, R_HUBP_ADDR=0x060a, R_HUBP_ADDRH=0x060b, HUBP_STR=0xdc,
       R_AUX_CTL=0x1f50, R_AUX_SWC=0x1f51, R_AUX_ARB=0x1f52, R_AUX_INT=0x1f53, R_AUX_STS=0x1f54, R_AUX_DATA=0x1f56, AUX_STR=0x1c, DIG_STR=0x100 };
static u32 dig_fe, dig_be, dig_en;

static int dc_layout(void){
    u32 v=amd_ipver(AMD_IP_DCN); u32 s=g_amd.ip[AMD_IP_DCN].base[2];
    if(!s&&(g_amd.family==AMD_FAM_RV||g_amd.family==AMD_FAM_YC||g_amd.family==AMD_FAM_GC_10_3_7||g_amd.family==AMD_FAM_GC_11_0_1))s=0x34C0;
    if(!s)return -1;
    if(!v){ if(g_amd.family==AMD_FAM_RV)v=AMD_IPV(2,1,0); else if(g_amd.family==AMD_FAM_YC)v=AMD_IPV(3,1,2); else if(g_amd.family==AMD_FAM_GC_10_3_7)v=AMD_IPV(3,1,6); else if(g_amd.family==AMD_FAM_GC_11_0_1)v=AMD_IPV(3,1,4); }
    if((v>>16)==2){ dig_fe=0x2068; dig_be=0x20af; dig_en=0x20b0; }
    else if(v==AMD_IPV(3,1,4)){ dig_fe=0x208b; dig_be=0x20b1; dig_en=0x20b2; }
    else if((v>>8)==0x0301){ dig_fe=0x208b; dig_be=0x20b0; dig_en=0x20b1; }
    else return -1;
    g_amd_dc.ver=v; g_amd_dc.seg2=s; return 0; }

int amd_dc_scan(void){
    amd_dc_t* d=&g_amd_dc; d->ok=0;
    if(!g_amd.found||!g_amd.mmio||dc_layout()<0)return -1;
    if(DR(R_OTG_CTL)==0xFFFFFFFFu)return -2;
    d->nump=AMD_DC_MAXP; d->numdig=5;
    for(int i=0;i<d->nump;i++){
        amd_otg_t* o=&d->otg[i]; u32 b=(u32)i*OTG_STR; o->en=DR(R_OTG_CTL+b)&1;
        o->htot=(DR(R_OTG_HT+b)&0x7FFF)+1; o->vtot=(DR(R_OTG_VT+b)&0x7FFF)+1;
        u32 hb=DR(R_OTG_HB+b), vb=DR(R_OTG_VB+b); o->hbs=hb&0x7FFF; o->hbe=(hb>>16)&0x7FFF; o->vbs=vb&0x7FFF; o->vbe=(vb>>16)&0x7FFF;
        o->odm_src=(DR(R_ODM_SRC+(u32)i*ODM_STR)>>16)&0xF;
        amd_hubp_t* h=&d->hubp[i]; u32 hb2=(u32)i*HUBP_STR; h->blank=DR(R_HUBP_CNTL+hb2)&1; h->fmt=DR(R_HUBP_CFG+hb2)&0x7F;
        u32 vp=DR(R_HUBP_VP+hb2); h->w=vp&0x3FFF; h->h=(vp>>16)&0x3FFF; h->pitch=(DR(R_HUBP_PITCH+hb2)&0x3FFF)+1;
        h->addr=((u64)(DR(R_HUBP_ADDRH+hb2)&0xFFFF)<<32)|DR(R_HUBP_ADDR+hb2); }
    for(int i=0;i<d->numdig;i++){ amd_dig_t* g=&d->dig[i]; u32 b=(u32)i*DIG_STR; g->en=DR(dig_en+b)&1; g->fe_src=DR(dig_fe+b)&7; u32 be=DR(dig_be+b); g->be_fe=(be>>8)&0x7F; g->mode=(be>>16)&7; }
    d->ok=1; return 0; }

static int aux_acquire(u32 a){
    if(((DR(R_AUX_ARB+a)>>2)&3)==2)return -1;
    u32 c=DR(R_AUX_CTL+a);
    if(!(c&1)){ c|=1|0x10; DW(R_AUX_CTL+a,c); amd_wait(g_amd_dc.seg2+R_AUX_CTL+a,0x20,0x20,20); c&=~0x10u; DW(R_AUX_CTL+a,c); amd_wait(g_amd_dc.seg2+R_AUX_CTL+a,0x20,0,20); }
    DW(R_AUX_ARB+a,DR(R_AUX_ARB+a)|0x10000);
    return (((DR(R_AUX_ARB+a)>>2)&3)==1)?0:-1; }
static void aux_release(u32 a){ DW(R_AUX_ARB+a,(DR(R_AUX_ARB+a)&~0x10000u)|0x20000); }

static int aux_xfer(u32 a,u8 cmd,u32 addr,const u8* wbuf,u8* rbuf,int len){
    int wr=!(cmd&1)&&wbuf; u32 n=len?4:3; if(wr)n+=(u32)len;
    DW(R_AUX_INT+a,DR(R_AUX_INT+a)|2);
    if(amd_wait(g_amd_dc.seg2+R_AUX_STS+a,1,0,1000))return -1;
    DW(R_AUX_SWC+a,(DR(R_AUX_SWC+a)&~0x001F00F0u)|(n<<16));
    DW(R_AUX_DATA+a,0x80000000u|((u32)((cmd<<4)|((addr>>16)&0xF))<<8));
    DW(R_AUX_DATA+a,((addr>>8)&0xFF)<<8); DW(R_AUX_DATA+a,(addr&0xFF)<<8);
    if(len)DW(R_AUX_DATA+a,(u32)(len-1)<<8);
    if(wr)for(int i=0;i<len;i++)DW(R_AUX_DATA+a,(u32)wbuf[i]<<8);
    DW(R_AUX_SWC+a,DR(R_AUX_SWC+a)|1);
    if(amd_wait(g_amd_dc.seg2+R_AUX_STS+a,1,1,4000))return -2;
    u32 st=DR(R_AUX_STS+a); if(st&0x00D75F80u)return -3;
    int nb=(int)((st>>24)&0x1F); if(!nb)return -4;
    u32 dv=DR(R_AUX_DATA+a)&~0x001F0000u; DW(R_AUX_DATA+a,dv); dv|=0x80000000u; DW(R_AUX_DATA+a,dv); DW(R_AUX_DATA+a,dv|1);
    u32 rep=(DR(R_AUX_DATA+a)>>8)&0xFF; if(rep>>4)return -5;
    nb--; if(wr||!rbuf)return 0; if(nb>len)return -6;
    for(int i=0;i<nb;i++)rbuf[i]=(u8)(DR(R_AUX_DATA+a)>>8);
    return nb; }

static int aux_retry(u32 a,u8 cmd,u32 addr,const u8* w,u8* r,int len){ int rc=-1; for(int t=0;t<4;t++){ rc=aux_xfer(a,cmd,addr,w,r,len); if(rc>=0)return rc; hr_delay_us(400); } return rc; }

int amd_dc_aux_i2c_read(int aux,u8 addr,u8 off,u8* buf,int len){
    if(!g_amd_dc.ok||aux<0||aux>4||len<0)return -1;
    u32 a=(u32)aux*AUX_STR; if(aux_acquire(a)<0){ aux_release(a); return -2; }
    int got=0, rc=aux_retry(a,0x4,addr,&off,0,1);
    while(rc>=0&&got<len){ int c=len-got>16?16:len-got; rc=aux_retry(a,0x5,addr,0,buf+got,c); if(rc<=0){ if(rc==0)rc=-7; break; } got+=rc; }
    aux_xfer(a,0x1,addr,0,0,0);
    aux_release(a); return rc<0?rc:got; }

static int edid_ok(const u8* e){ static const u8 h[8]={0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0}; for(int i=0;i<8;i++)if(e[i]!=h[i])return 0; u8 s=0; for(int i=0;i<128;i++)s=(u8)(s+e[i]); return s==0; }

int amd_dc_edid(u8* out,int max){
    amd_dc_t* d=&g_amd_dc; if(!d->ok&&amd_dc_scan()<0)return -1;
    if(d->edid_len<=0){ d->edid_aux=-1;
        for(int x=0;x<5;x++){ if(amd_dc_aux_i2c_read(x,0x50,0,d->edid,128)!=128||!edid_ok(d->edid))continue;
            d->edid_aux=x; d->edid_len=128;
            if(d->edid[126]&&amd_dc_aux_i2c_read(x,0x50,128,d->edid+128,128)==128){ d->edid_len=256; }
            break; }
        if(d->edid_aux<0)return -2; }
    int n=d->edid_len<max?d->edid_len:max; for(int i=0;i<n;i++)out[i]=d->edid[i]; return n; }

static const char* fmt_name(u32 f){ switch(f){ case 8: return "ARGB8888"; case 9: return "ABGR8888"; case 10: return "ARGB2101010"; case 1: return "ARGB1555"; case 3: return "RGB565"; default: return "?"; } }
static const char* dig_mode(u32 m){ switch(m){ case 0: return "DP"; case 2: return "HDMI/DVI"; case 3: return "HDMI"; case 5: return "DP-MST"; default: return "?"; } }

int amd_dc_info(char* buf,int n){
    amd_dc_t* d=&g_amd_dc; char t[224]; char* e=buf+n-1; char* q=buf;
    if(n<2)return 0;
    if(!d->ok&&amd_dc_scan()<0){ const char* m="dc: display block not available\n"; while(*m&&q<e)*q++=*m++; *q=0; return (int)(q-buf); }
    for(int i=-1;i<d->nump+d->numdig;i++){ char* p=t;
        if(i<0){ p=scopy(p,"DCN "); p=udec(p,d->ver>>16); p=scopy(p,"."); p=udec(p,(d->ver>>8)&0xFF); p=scopy(p,"."); p=udec(p,d->ver&0xFF); p=scopy(p," seg2=0x"); p=hx(p,d->seg2,4); }
        else if(i<d->nump){ amd_otg_t* o=&d->otg[i]; amd_hubp_t* h=&d->hubp[i];
            p=scopy(p,"OTG"); p=udec(p,(u32)i); p=scopy(p,o->en?" on  ":" off ");
            if(o->en){ p=udec(p,o->htot); p=scopy(p,"x"); p=udec(p,o->vtot); p=scopy(p," hblank "); p=udec(p,o->hbs); p=scopy(p,"-"); p=udec(p,o->hbe); p=scopy(p," vblank "); p=udec(p,o->vbs); p=scopy(p,"-"); p=udec(p,o->vbe); p=scopy(p," src "); p=udec(p,o->odm_src); }
            p=scopy(p," | HUBP"); p=udec(p,(u32)i); p=scopy(p,h->blank?" blank":" scan ");
            if(!h->blank){ p=scopy(p," "); p=udec(p,h->w); p=scopy(p,"x"); p=udec(p,h->h); p=scopy(p," pitch "); p=udec(p,h->pitch); p=scopy(p," "); p=scopy(p,fmt_name(h->fmt)); p=scopy(p," @0x"); p=uhex(p,h->addr,12); } }
        else { int k=i-d->nump; amd_dig_t* g=&d->dig[k]; if(!g->en)continue;
            p=scopy(p,"DIG"); p=udec(p,(u32)k); p=scopy(p," on "); p=scopy(p,dig_mode(g->mode)); p=scopy(p," fe "); p=udec(p,g->fe_src); p=scopy(p," be_fe 0x"); p=hx(p,g->be_fe,2); }
        *p++='\n'; *p=0; for(char* s=t;*s&&q<e;)*q++=*s++; }
    if(d->edid_len>0){ char* p=t; p=scopy(p,"EDID via AUX"); p=udec(p,(u32)d->edid_aux); p=scopy(p," "); p=udec(p,(u32)d->edid_len); p=scopy(p," bytes\n"); for(char* s=t;*s&&q<e;)*q++=*s++; }
    *q=0; return (int)(q-buf); }
