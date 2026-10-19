#include "include/nvx/base.h"
#include "include/nvx/drivers/video/nvidia/nvgpu.h"
#include "include/nvx/drivers/video/nvidia/nvdisp.h"
#include "include/nvx/drivers/video/nvidia/nvmon.h"

static int nvm_rd16(const u8* v,int n,int o){ return (o>=0&&o+1<n)?(v[o]|(v[o+1]<<8)):0; }
static u32 nvm_rd32(const u8* v,int n,int o){ return (o>=0&&o+3<n)?((u32)v[o]|((u32)v[o+1]<<8)|((u32)v[o+2]<<16)|((u32)v[o+3]<<24)):0; }
static int nvm_i2c_port(const u8* v,int n,int dcb,int idx){
    int t=nvm_rd16(v,n,dcb+4); if(!t||t+4>n)return idx;
    int ver=v[t], hl=v[t+1], cnt=v[t+2], es=v[t+3]; if(idx>=cnt||es<4)return idx;
    u32 ent=nvm_rd32(v,n,t+hl+idx*es);
    if(ver>=0x41){ int drive=(int)(ent&0x1F); return drive==0x1F?-1:drive; }
    return idx;
}
int nvm_outputs(const u8* v,int n,int dcb,nvm_out_t* out,int max){
    if(dcb<=0||dcb+8>n||v[dcb]<0x30||nvm_rd32(v,n,dcb+6)!=0x4EDCBDCBu)return 0;
    int hl=v[dcb+1], cnt=v[dcb+2], es=v[dcb+3]; int conn=nvm_rd16(v,n,dcb+0x14), chl=0, ccnt=0, ces=0;
    if(conn&&conn+4<n){ chl=v[conn+1]; ccnt=v[conn+2]; ces=v[conn+3]; }
    int k=0;
    for(int e=0;e<cnt&&k<max;e++){ u32 d0=nvm_rd32(v,n,dcb+hl+e*es); u8 t=(u8)(d0&0xF);
        if(t==0xE)break; if(t==0xF||d0==0)continue;
        nvm_out_t* o=&out[k++]; o->type=t; o->i2c=(u8)((d0>>4)&0xF); o->heads=(u8)((d0>>8)&0xF); o->conn=(u8)((d0>>12)&0xF); { u8 m=(u8)((d0>>24)&0xF); u8 i=0; while(i<4&&m&&!(m&(1u<<i)))i++; o->sor=m?i:0xFF; }
        o->conntype=0xFF; if(conn&&o->conn<ccnt&&ces>0){ int ce=conn+chl+o->conn*ces; if(ce<n)o->conntype=v[ce]; }
        int p=nvm_i2c_port(v,n,dcb,o->i2c); o->port=(u8)(p<0?0xFF:p); }
    return k;
}
const char* nvm_conn(u8 c){ switch(c){ case 0x00: return "VGA"; case 0x30: return "DVI-I"; case 0x31: return "DVI-D"; case 0x40: case 0x41: return "LVDS"; case 0x46: return "DisplayPort"; case 0x47: return "eDP"; case 0x48: return "mini DisplayPort"; case 0x60: case 0x61: return "HDMI"; case 0x63: return "mini HDMI"; case 0x71: return "USB-C"; default: return "connector"; } }
int nvm_sor_index(u8 mask){ for(int i=0;i<4;i++) if(mask&(1u<<i))return i; return -1; }
const char* nvm_sig(u8 t){ switch(t){ case 0: return "analog"; case 2: return "TMDS (HDMI/DVI)"; case 3: return "LVDS"; case 6: return "DisplayPort"; default: return "other"; } }

int nvm_edid_parse(const u8* e,nvm_edid_t* m){
    m->ok=0; m->name[0]=0;
    static const u8 hdr[8]={0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0}; for(int i=0;i<8;i++) if(e[i]!=hdr[i])return -1;
    u8 s=0; for(int i=0;i<128;i++)s=(u8)(s+e[i]); if(s)return -2;
    u16 mf=(u16)((e[8]<<8)|e[9]); m->maker[0]=(char)('A'-1+((mf>>10)&31)); m->maker[1]=(char)('A'-1+((mf>>5)&31)); m->maker[2]=(char)('A'-1+(mf&31)); m->maker[3]=0;
    m->product=(u16)(e[10]|(e[11]<<8));
    for(int b=54;b<=108;b+=18) if(e[b]==0&&e[b+1]==0&&e[b+3]==0xFC){ int o=0; for(int i=5;i<18&&o<13;i++){ char c=(char)e[b+i]; if(c=='\n')break; m->name[o++]=c; } while(o>0&&m->name[o-1]==' ')o--; m->name[o]=0; }
    const u8* d=e+54; m->pclk_khz=(u32)(d[0]|(d[1]<<8))*10u;
    if(m->pclk_khz){ m->hact=(u16)(d[2]|((d[4]&0xF0)<<4)); u16 hbl=(u16)(d[3]|((d[4]&0x0F)<<8)); m->vact=(u16)(d[5]|((d[7]&0xF0)<<4)); u16 vbl=(u16)(d[6]|((d[7]&0x0F)<<8));
        m->htot=(u16)(m->hact+hbl); m->vtot=(u16)(m->vact+vbl); m->hz=(m->htot&&m->vtot)?(u16)(((u64)m->pclk_khz*1000ull+(u64)m->htot*m->vtot/2)/((u64)m->htot*m->vtot)):0; }
    m->ok=1; return 0;
}
static u32 g_nvm_i2c_addr;
static void nvm_i2c_set(int bit,int high){ u32 v=nvr32(g_nvm_i2c_addr); if(high)v|=bit; else v&=~(u32)bit; nvw32(g_nvm_i2c_addr,v); nv_udelay(5); }
static int nvm_scl(void){ return (nvr32(g_nvm_i2c_addr)>>4)&1; }
static int nvm_sda(void){ return (nvr32(g_nvm_i2c_addr)>>5)&1; }
static int nvm_scl_high(void){ nvm_i2c_set(1,1); for(int t=0;t<2200;t++){ if(nvm_scl())return 1; nv_udelay(1); } return 0; }
static int nvm_start(void){ nvm_i2c_set(2,1); if(!nvm_scl_high())return 0; nvm_i2c_set(2,0); nvm_i2c_set(1,0); return 1; }
static void nvm_stop(void){ nvm_i2c_set(1,0); nvm_i2c_set(2,0); nvm_scl_high(); nvm_i2c_set(2,1); }
static int nvm_wbyte(u8 b){
    for(int i=7;i>=0;i--){ nvm_i2c_set(2,(b>>i)&1); if(!nvm_scl_high())return 0; nvm_i2c_set(1,0); }
    nvm_i2c_set(2,1); if(!nvm_scl_high())return 0; int ack=!nvm_sda(); nvm_i2c_set(1,0); return ack;
}
static int nvm_rbyte(int ack,u8* out){
    u8 b=0; nvm_i2c_set(2,1);
    for(int i=0;i<8;i++){ if(!nvm_scl_high())return 0; b=(u8)((b<<1)|nvm_sda()); nvm_i2c_set(1,0); }
    nvm_i2c_set(2,ack?0:1); if(!nvm_scl_high())return 0; nvm_i2c_set(1,0); nvm_i2c_set(2,1); *out=b; return 1;
}
int nvm_read_edid(int port,u8* e){
    g_nvm_i2c_addr=0x00D014u+(u32)port*0x20u; nvw32(g_nvm_i2c_addr,0x00000007); nv_udelay(10);
    int ok=0;
    if(nvm_start()&&nvm_wbyte(0xA0)&&nvm_wbyte(0x00)&&nvm_start()&&nvm_wbyte(0xA1)){ ok=1; for(int i=0;i<128&&ok;i++) ok=nvm_rbyte(i<127,&e[i]); }
    nvm_stop(); nvw32(g_nvm_i2c_addr,0x00000007);
    return ok;
}

void nvm_decode_head(u32 total,u32 synce,u32 blanke,u32 blanks,u32 pclk,nvm_head_t* h){
    (void)synce; h->htot=(u16)(total&0xFFFF); h->vtot=(u16)(total>>16);
    u16 hbe=(u16)(blanke&0xFFFF), vbe=(u16)(blanke>>16), hbs=(u16)(blanks&0xFFFF), vbs=(u16)(blanks>>16);
    h->hact=(u16)(hbs>hbe?hbs-hbe:0); h->vact=(u16)(vbs>vbe?vbs-vbe:0); h->pclk=pclk;
    h->active=(h->htot&&h->vtot&&h->hact&&h->vact&&h->htot!=0xFFFF&&pclk&&pclk!=0xFFFFFFFFu);
    h->hz=h->active?(u16)(((u64)pclk+(u64)h->htot*h->vtot/2)/((u64)h->htot*h->vtot)):0;
}
const char* nvm_proto(u32 p){ switch(p){ case 0: return "LVDS"; case 1: return "HDMI/DVI (TMDS link A)"; case 2: return "HDMI/DVI (TMDS link B)"; case 5: return "DVI dual-link"; case 8: return "DisplayPort (A)"; case 9: return "DisplayPort (B)"; default: return "other"; } }
