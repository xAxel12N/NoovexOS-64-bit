#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"


u8 rtw_phymap[RTW_EFUSE_PHY_SZ];
u8 rtw_logmap[RTW_EFUSE_LOG_SZ];
int rtw_efuse_valid=0;

static u8  rtw_pci_r8 (u32 m,u32 o){ return *(volatile u8* )(u64)(m+o); }
static u16 rtw_pci_r16(u32 m,u32 o){ return *(volatile u16*)(u64)(m+o); }
static u32 rtw_pci_r32(u32 m,u32 o){ return *(volatile u32*)(u64)(m+o); }
static void rtw_pci_w8 (u32 m,u32 o,u8  v){ *(volatile u8* )(u64)(m+o)=v; }
static void rtw_pci_w16(u32 m,u32 o,u16 v){ *(volatile u16*)(u64)(m+o)=v; }
static void rtw_pci_w32(u32 m,u32 o,u32 v){ *(volatile u32*)(u64)(m+o)=v; }
const rtw_bus rtw_bus_pcie={ "PCIe", RTW_BUS_PCIE, rtw_pci_r8, rtw_pci_r16, rtw_pci_r32, rtw_pci_w8, rtw_pci_w16, rtw_pci_w32 };
const rtw_bus* g_rtw_bus=&rtw_bus_pcie;
u8  rtw_r8 (u32 m,u32 o){ return g_rtw_bus->r8(m,o); }
u16 rtw_r16(u32 m,u32 o){ return g_rtw_bus->r16(m,o); }
u32 rtw_r32(u32 m,u32 o){ return g_rtw_bus->r32(m,o); }
void rtw_w8 (u32 m,u32 o,u8  v){ g_rtw_bus->w8(m,o,v); }
void rtw_w16(u32 m,u32 o,u16 v){ g_rtw_bus->w16(m,o,v); }
void rtw_w32(u32 m,u32 o,u32 v){ g_rtw_bus->w32(m,o,v); }

static void rtw_efuse_prep(u32 m){
    u32 v=rtw_r32(m,0x0034); v&=~((1u<<8)|(1u<<9)); rtw_w32(m,0x0034,v);
    u8 ldo=rtw_r8(m,0x0037); ldo&=~(1u<<7); rtw_w8(m,0x0037,ldo);
}

int rtw_efuse_dump_phy(u32 m,u8* map,int size){
    int bad=0;
    rtw_efuse_prep(m);
    u32 c=rtw_r32(m,0x0030);
    for(int a=0;a<size;a++){
        c=rtw_r32(m,0x0030);
        c&=~((0xFFu)|(0x3FFu<<8));
        c|=(((u32)a & 0x3FFu)<<8);
        c&=~(1u<<31);
        rtw_w32(m,0x0030,c);
        int t=2000;
        do{ rtw_delay_us(1); c=rtw_r32(m,0x0030); }while(!(c&(1u<<31)) && --t>0);
        if(t<=0){ map[a]=0xFF; bad++; if(bad>=64){ for(int z=a+1;z<size;z++)map[z]=0xFF; break; } }
        else map[a]=(u8)(c & 0xFF);
    }
    return bad;
}

int rtw_efuse_logical(const u8* phy,int physize,int protect,u8* log,int logsize){
    for(int i=0;i<logsize;i++)log[i]=0xFF;
    int pi=0, limit=physize-protect;
    while(pi<limit){
        u8 h1=phy[pi];
        if(h1==0xFF)break;
        u8 blk,wen;
        if((h1&0x1F)==0x0F){
            if(pi+1>=limit)break;
            u8 h2=phy[pi+1];
            pi+=2;
            blk=(u8)(((h2&0xF0)>>1)|((h1>>5)&0x07));
            wen=(u8)(h2&0x0F);
        } else {
            blk=(u8)((h1&0xF0)>>4);
            wen=(u8)(h1&0x0F);
            pi+=1;
        }
        for(int i=0;i<4;i++){
            if(((~(wen>>i))&1)==0)continue;
            int li=(blk<<3)+(i<<1);
            if(li+1>=logsize){ return pi>0?pi:-1; }
            if(pi+1>=physize){ return pi>0?pi:-1; }
            log[li]=phy[pi]; log[li+1]=phy[pi+1];
            pi+=2;
        }
    }
    return pi;
}

void rtw_hexline(const u8* p,int off,int n,char* out){
    char* q=out;
    q=scopy(q,"  "); q=hx(q,(u32)off,3); q=scopy(q,"  ");
    for(int i=0;i<n;i++){ q=hx(q,p[off+i],2); *q++=' '; if(i==7)*q++=' '; }
    *q=0;
}

rtw_regw rtw_mactbl[512];
static int rtw_mactbl_n=0;

static int rtw_hexval(const char* s,int n,u32* out){
    u32 v=0; int got=0; if(n>8)return 0;
    for(int i=0;i<n;i++){
        char c=s[i]; u32 d;
        if(c>='0'&&c<='9')d=(u32)(c-'0');
        else if(c>='a'&&c<='f')d=(u32)(c-'a'+10);
        else if(c>='A'&&c<='F')d=(u32)(c-'A'+10);
        else break;
        v=(v<<4)|d; got=1;
    }
    if(!got)return 0;
    *out=v; return 1;
}

int rtw_tbl_parse(const char* txt,int len){
    rtw_mactbl_n=0;
    int i=0;
    while(i<len && rtw_mactbl_n<512){
        while(i<len && (txt[i]=='\r'||txt[i]=='\n'||txt[i]==' '||txt[i]=='\t'))i++;
        if(i>=len)break;
        if(txt[i]=='#'||txt[i]==';'){ while(i<len && txt[i]!='\n')i++; continue; }
        u32 f[3]; int nf=0;
        while(nf<3 && i<len){
            if(txt[i]=='0'&&i+1<len&&(txt[i+1]=='x'||txt[i+1]=='X'))i+=2;
            int st=i;
            while(i<len && txt[i]!=' ' && txt[i]!='\t' && txt[i]!='\r' && txt[i]!='\n')i++;
            if(i>st && rtw_hexval(txt+st,i-st,&f[nf]))nf++;
            while(i<len && (txt[i]==' '||txt[i]=='\t'))i++;
            if(i<len && (txt[i]=='\r'||txt[i]=='\n'))break;
        }
        while(i<len && txt[i]!='\n')i++;
        if(nf==3 && (f[1]==1||f[1]==2||f[1]==4||f[1]==5)){
            rtw_mactbl[rtw_mactbl_n].off=f[0];
            rtw_mactbl[rtw_mactbl_n].sz=(u8)f[1];
            rtw_mactbl[rtw_mactbl_n].val=f[2];
            rtw_mactbl_n++;
        }
    }
    return rtw_mactbl_n;
}

u32 rtw_rdsz(u32 m,u32 o,u8 sz){
    if(sz==1)return rtw_r8(m,(u16)o);
    if(sz==2)return rtw_r16(m,(u16)o);
    return rtw_r32(m,(u16)o);
}
void rtw_delay_us(u32 us){ if(us>200000u)us=200000u; hr_delay_us(us); }
static int rtw_rf_path_ok(int p){ int n=g_rtw_chip->n_paths?g_rtw_chip->n_paths:1; return p>=0&&p<n&&p<2; }
static u32 rtw_rf_base(int p){ u32 b=g_rtw_chip->rf_base[p]; return b?b:(p?0x2C00u:0x2800u); }
static u32 rtw_rf_sipi(int p){ u32 b=g_rtw_chip->rf_sipi[p]; return b?b:(p?0x0E90u:0x0C90u); }
static u32 rtw_mask_shift(u32 mask){ u32 sh=0; while(sh<31&&!((mask>>sh)&1u))sh++; return sh; }
u32 rtw_rf_read(u32 m,int path,u32 addr,u32 mask){
    if(!rtw_rf_path_ok(path))return 0xFFFFFFFFu;
    mask&=0x000FFFFFu; if(!mask)return 0;
    if(g_rtw_chip->rf_read)return (g_rtw_chip->rf_read(m,path,addr&0xFFu)&mask)>>rtw_mask_shift(mask);
    return (rtw_r32(m,rtw_rf_base(path)+((addr&0xFFu)<<2))&mask)>>rtw_mask_shift(mask);
}
void rtw_rf_write(u32 m,int path,u32 addr,u32 mask,u32 val){
    if(!rtw_rf_path_ok(path))return;
    mask&=0x000FFFFFu; if(!mask)return;
    u32 sh=rtw_mask_shift(mask); addr&=0xFFu;
    if(g_rtw_chip->rf_mix&&addr){ u32 a=rtw_rf_base(path)+(addr<<2); u32 c=rtw_r32(m,a); rtw_w32(m,a,(c&~mask)|((val<<sh)&mask)); rtw_delay_us(1); return; }
    if(mask!=0x000FFFFFu){ u32 c=rtw_rf_read(m,path,addr,0x000FFFFFu); val=(c&~mask)|((val<<sh)&mask); }
    rtw_w32(m,rtw_rf_sipi(path),((addr<<20)|(val&0x000FFFFFu))&0x0FFFFFFFu);
    rtw_delay_us(13);
}
void rtw_bb_write_mask(u32 m,u32 addr,u32 mask,u32 val){ u32 sh=rtw_mask_shift(mask); u32 c=rtw_r32(m,addr); c=(c&~mask)|((val<<sh)&mask); rtw_w32(m,addr,c); }
u32 rtw_bb_read_mask(u32 m,u32 addr,u32 mask){ if(!mask)return 0; return (rtw_r32(m,addr)&mask)>>rtw_mask_shift(mask); }
void rtw_rf_write_a(u32 m,u32 addr,u32 data){ rtw_rf_write(m,0,addr,0x000FFFFFu,data); }
int rtw_efuse_phy_sz(void){ int n=g_rtw_chip->efuse_phy; return n>0&&n<=RTW_EFUSE_PHY_SZ?n:512; }
int rtw_efuse_log_sz(void){ int n=g_rtw_chip->efuse_log; return n>0&&n<=RTW_EFUSE_LOG_SZ?n:512; }
int rtw_efuse_prot_sz(void){ int n=g_rtw_chip->efuse_prot; return n>0&&n<rtw_efuse_phy_sz()?n:96; }
void rtw_efuse_apply(void){ if(rtw_efuse_valid&&g_rtw_chip->efuse_parse)g_rtw_chip->efuse_parse(rtw_logmap,rtw_efuse_log_sz(),&g_rtw_efuse); }
int rtw_efuse_load(u32 m){
    rtw_efuse_dump_phy(m,rtw_phymap,rtw_efuse_phy_sz());
    int used=rtw_efuse_logical(rtw_phymap,rtw_efuse_phy_sz(),rtw_efuse_prot_sz(),rtw_logmap,rtw_efuse_log_sz());
    rtw_efuse_valid=(used>0);
    rtw_efuse_apply();
    return used;
}
#include "drivers/net/wifi/realtek/rtw88/rtw_tables.h"

