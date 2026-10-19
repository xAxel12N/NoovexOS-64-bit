#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"


u8 rtw_phymap[RTW_EFUSE_PHY_SZ];
u8 rtw_logmap[RTW_EFUSE_LOG_SZ];
int rtw_efuse_valid=0;

u8  rtw_r8 (u32 m,u32 o){ return *(volatile u8* )(u64)(m+o); }
u16 rtw_r16(u32 m,u32 o){ return *(volatile u16*)(u64)(m+o); }
u32 rtw_r32(u32 m,u32 o){ return *(volatile u32*)(u64)(m+o); }
void rtw_w8 (u32 m,u32 o,u8  v){ *(volatile u8* )(u64)(m+o)=v; }
void rtw_w16(u32 m,u32 o,u16 v){ *(volatile u16*)(u64)(m+o)=v; }
void rtw_w32(u32 m,u32 o,u32 v){ *(volatile u32*)(u64)(m+o)=v; }

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
void rtw_delay_us(u32 us){ if(us>200000u)us=200000u; for(u32 i=0;i<us;i++){ for(volatile int d=0;d<8;d++) io_wait(); } }
#define RTW_RFA_SIPI 0xC90u
void rtw_rf_write_a(u32 m,u32 addr,u32 data){
    u32 daa=(((addr&0xFF)<<20)|(data&0x000FFFFFu))&0x0FFFFFFFu;
    rtw_w32(m,RTW_RFA_SIPI,daa);
    rtw_delay_us(13);
}
#include "drivers/net/wifi/rtw_tables.h"

