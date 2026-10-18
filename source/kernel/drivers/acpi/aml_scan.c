#include "include/nvx/base.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/core/power.h"
#include "include/nvx/drivers/acpi/aml_scan.h"

#define AMLT_MAX 33

amlnode g_amln[AMLN_MAX]; static int g_amln_n=0;
static const u8* g_amlt[AMLT_MAX]; static u32 g_amll[AMLT_MAX]; int g_amlt_n=0;

amldev g_amld[AMLD_MAX]; int g_amld_n=0;
static int aml_segch(u8 c,int first){ return (c>='A'&&c<='Z')||c=='_'||(!first&&c>='0'&&c<='9'); }
static int aml_pkglen(const u8* p,u32 avail,u32* len){ if(!avail)return 0; u8 b0=p[0]; int n=b0>>6; if((u32)n+1>avail)return 0;
    if(!n){ *len=b0&0x3Fu; return 1; } if(b0&0x30)return 0; u32 v=b0&0x0Fu; for(int i=0;i<n;i++)v|=(u32)p[1+i]<<(4+8*i); *len=v; return n+1; }
static int aml_namestr(const u8* p,u32 avail,char* out,int cap){
    u32 i=0; int o=0;
    while(i<avail&&(p[i]=='\\'||p[i]=='^')){ if(o<cap-1)out[o++]=(char)p[i]; i++; }
    if(i>=avail)return 0;
    int segs=1; if(p[i]==0x2E){ segs=2; i++; } else if(p[i]==0x2F){ if(i+1>=avail)return 0; segs=p[i+1]; i+=2; if(segs<1||segs>8)return 0; } else if(p[i]==0){ out[o]=0; return (int)i+1; }
    for(int s=0;s<segs;s++){ if(i+4>avail)return 0; for(int k=0;k<4;k++) if(!aml_segch(p[i+k],k==0))return 0;
        if(s>0&&o<cap-1)out[o++]='.'; for(int k=0;k<4;k++) if(o<cap-1)out[o++]=(char)p[i+k]; i+=4; }
    out[o]=0; return (int)i; }
static void aml_norm(const char* in,char* out,int cap){ int o=0; int i=0;
    while(in[i]){ if(in[i]=='\\'||in[i]=='^'||in[i]=='.'){ if(o<cap-1)out[o++]=in[i]; i++; continue; }
        int j=i; while(in[j]&&in[j]!='.')j++; int k=j; while(k>i&&in[k-1]=='_')k--; for(int q=i;q<k;q++) if(o<cap-1)out[o++]=in[q]; i=j; }
    out[o]=0; }
static int aml_streq(const char* a,const char* b){ while(*a&&*a==*b){a++;b++;} return *a==*b; }
static void aml_join(const char* parent,const char* rel,char* out,int cap){
    int o=0;
    if(rel[0]=='\\'){ for(int i=0;rel[i]&&o<cap-1;i++)out[o++]=rel[i]; out[o]=0; return; }
    char base[72]; int bl=0; for(int i=0;parent[i]&&bl<71;i++)base[bl++]=parent[i]; base[bl]=0;
    int i=0; while(rel[i]=='^'){ while(bl>1&&base[bl-1]!='.')bl--; if(bl>1)bl--; else bl=1; base[bl]=0; i++; }
    for(int k=0;k<bl&&o<cap-1;k++)out[o++]=base[k];
    if(rel[i]){ if(o>1&&o<cap-1)out[o++]='.'; for(;rel[i]&&o<cap-1;i++)out[o++]=rel[i]; }
    out[o]=0; }
static void aml_walk_table(int ti){
    const u8* b=g_amlt[ti]; u32 n=g_amll[ti];
    int stack[48]; int sp=0; u32 i=0;
    while(i<n){
        while(sp>0&&i>=g_amln[stack[sp-1]].e)sp--;
        u32 lim=sp?g_amln[stack[sp-1]].e:n;
        int ol=0, dev=0; if(b[i]==0x5B&&i+1<n&&b[i+1]==0x82){ ol=2; dev=1; } else if(b[i]==0x10)ol=1;
        if(ol&&g_amln_n<AMLN_MAX){ u32 pl=0; int pb=aml_pkglen(b+i+ol,lim-(i+ol>lim?lim:i+ol),&pl);
            if(pb&&pl>(u32)pb&&i+ol+pl<=lim){ char nm[72]; int nb=aml_namestr(b+i+ol+pb,i+ol+pl-(i+ol+pb),nm,72);
                if(nb>0&&nm[0]){ amlnode* N=&g_amln[g_amln_n]; N->tbl=ti; N->op=i; N->s=i+ol+(u32)pb+(u32)nb; N->e=i+ol+pl; N->parent=sp?stack[sp-1]:-1; N->dev=dev;
                    aml_join(sp?g_amln[stack[sp-1]].path:"\\",nm,N->path,72);
                    if(sp<48)stack[sp++]=g_amln_n; g_amln_n++; i=N->s; continue; } } }
        i++; } }
static int aml_in_child(int k,u32 pos){ for(int c=k+1;c<g_amln_n;c++){ if(g_amln[c].tbl!=g_amln[k].tbl)break; if(g_amln[c].op>=g_amln[k].e)break; if(g_amln[c].dev&&pos>=g_amln[c].op&&pos<g_amln[c].e){ int p=g_amln[c].parent; while(p>=0&&p!=k)p=g_amln[p].parent; if(p==k)return (int)(g_amln[c].e-pos); } } return 0; }
static void aml_eisa(u32 id,char* o){ u8 b0=(u8)id, b1=(u8)(id>>8), b2=(u8)(id>>16), b3=(u8)(id>>24); u16 w=(u16)((b0<<8)|b1); static const char* H="0123456789ABCDEF";
    o[0]=(char)(((w>>10)&0x1F)+0x40); o[1]=(char)(((w>>5)&0x1F)+0x40); o[2]=(char)((w&0x1F)+0x40); o[3]=H[b2>>4]; o[4]=H[b2&15]; o[5]=H[b3>>4]; o[6]=H[b3&15]; o[7]=0; }
static int aml_int(const u8* p,u32 avail,u64* v){ if(!avail)return 0; switch(p[0]){ case 0x00: *v=0; return 1; case 0x01: *v=1; return 1; case 0xFF: *v=~0ULL; return 1;
    case 0x0A: if(avail<2)return 0; *v=p[1]; return 2; case 0x0B: if(avail<3)return 0; *v=(u64)p[1]|((u64)p[2]<<8); return 3;
    case 0x0C: if(avail<5)return 0; *v=(u64)p[1]|((u64)p[2]<<8)|((u64)p[3]<<16)|((u64)p[4]<<24); return 5;
    case 0x0E: if(avail<9)return 0; *v=0; for(int i=0;i<8;i++)*v|=(u64)p[1+i]<<(8*i); return 9; } return 0; }
static void aml_copy_src(const u8* p,u32 avail,char* out){ int o=0; for(u32 i=0;i<avail&&p[i]&&o<71;i++){ u8 c=p[i]; if(c<0x20||c>0x7E){ o=0; break; } out[o++]=(char)c; } out[o]=0; }
static void aml_scan_dev(int k){
    if(g_amld_n>=AMLD_MAX)return; amldev* D=&g_amld[g_amld_n]; for(u32 i=0;i<sizeof(*D);i++)((u8*)D)[i]=0; D->node=k;
    const u8* b=g_amlt[g_amln[k].tbl]; u32 s=g_amln[k].s, e=g_amln[k].e;
    static const u8 P1[]={'P','N','P','0','C','5','0',0}, P2[]={'A','C','P','I','0','C','5','0',0}, P3[]={0x0C,0x41,0xD0,0x0C,0x50};
    for(u32 j=s;j<e;){
        if(b[j]==0x5B){ int skip=aml_in_child(k,j); if(skip){ j+=(u32)skip; continue; } }
        u32 av=e-j;
        if(av>=8&&b[j]=='P'){ int m=1; for(int q=0;q<8;q++) if(b[j+q]!=P1[q]){ m=0; break; } if(m)D->is_hidi2c=1; }
        if(av>=9&&b[j]=='A'){ int m=1; for(int q=0;q<9;q++) if(b[j+q]!=P2[q]){ m=0; break; } if(m)D->is_hidi2c=1; }
        if(av>=5&&b[j]==0x0C){ int m=1; for(int q=0;q<5;q++) if(b[j+q]!=P3[q]){ m=0; break; } if(m)D->is_hidi2c=1; }
        if(av>=6&&b[j]==0x08&&b[j+1]=='_'){ const u8* nmp=b+j+1;
            if((nmp[1]=='H'&&nmp[2]=='I'&&nmp[3]=='D')&&!D->hid[0]){ const u8* v=b+j+5; u32 va=av-5; if(va&&v[0]==0x0D){ int o=0; for(u32 q=1;q<va&&v[q]&&o<11;q++)D->hid[o++]=(char)v[q]; D->hid[o]=0; } else { u64 x; if(aml_int(v,va,&x)==5)aml_eisa((u32)x,D->hid); } }
            if(nmp[1]=='A'&&nmp[2]=='D'&&nmp[3]=='R'){ u64 x; if(aml_int(b+j+5,av-5,&x)){ D->adr=x; D->has_adr=1; } } }
        if(b[j]==0x8E&&av>=19){ u32 L=(u32)b[j+1]|((u32)b[j+2]<<8); if(L>=15&&L+3<=av&&(b[j+3]==1||b[j+3]==2)&&b[j+5]==1){ u32 tdl=(u32)b[j+10]|((u32)b[j+11]<<8);
                if(tdl>=6&&12+tdl<=L+3&&D->ni2c<6){ aml_i2cres* r=&D->i2c[D->ni2c]; r->speed=(u32)b[j+12]|((u32)b[j+13]<<8)|((u32)b[j+14]<<16)|((u32)b[j+15]<<24); r->addr=(u16)(b[j+16]|(b[j+17]<<8));
                    aml_copy_src(b+j+12+tdl,L+3-(12+tdl),r->ctrl); int dup=0; for(int q=0;q<D->ni2c;q++) if(D->i2c[q].addr==r->addr&&aml_streq(D->i2c[q].ctrl,r->ctrl))dup=1; if(!dup)D->ni2c++; j+=L+3; continue; } } }
        if(b[j]==0x8C&&av>=23&&!D->has_gpio){ u32 L=(u32)b[j+1]|((u32)b[j+2]<<8); if(L>=20&&L+3<=av&&b[j+4]==0){ u32 pto=(u32)b[j+14]|((u32)b[j+15]<<8), rno=(u32)b[j+17]|((u32)b[j+18]<<8);
                if(pto+2<=L+3&&rno<L+3){ D->gpio.pin=(u16)(b[j+pto]|(b[j+pto+1]<<8)); u16 fl=(u16)(b[j+7]|(b[j+8]<<8)); D->gpio.level=!(fl&1); D->gpio.active_low=((fl>>1)&3)==1; aml_copy_src(b+j+rno,L+3-rno,D->gpio.ctrl); D->has_gpio=1; j+=L+3; continue; } } }
        if(b[j]==0x86&&av>=12&&b[j+1]==9&&b[j+2]==0&&!D->has_mem){ D->mem_base=(u32)b[j+4]|((u32)b[j+5]<<8)|((u32)b[j+6]<<16)|((u32)b[j+7]<<24); D->mem_len=(u32)b[j+8]|((u32)b[j+9]<<8)|((u32)b[j+10]<<16)|((u32)b[j+11]<<24); if(D->mem_base&&D->mem_len)D->has_mem=1; j+=12; continue; }
        j++; }
    g_amld_n++; }
void aml_scan_tables(void){
    g_amln_n=0; g_amld_n=0;
    for(int t=0;t<g_amlt_n;t++)aml_walk_table(t);
    for(int k=0;k<g_amln_n;k++) if(g_amln[k].dev)aml_scan_dev(k); }
int aml_find_dev(const char* path){
    char want[72]; aml_norm(path,want,72);
    for(int i=0;i<g_amld_n;i++){ char have[72]; aml_norm(g_amln[g_amld[i].node].path,have,72); if(aml_streq(have,want))return i; }
    const char* ls=want; for(const char* p=want;*p;p++) if(*p=='.'||*p=='\\')ls=p+1;
    for(int i=0;i<g_amld_n;i++){ char have[72]; aml_norm(g_amln[g_amld[i].node].path,have,72); const char* hs=have; for(const char* p=have;*p;p++) if(*p=='.'||*p=='\\')hs=p+1; if(*ls&&aml_streq(hs,ls))return i; }
    return -1; }
void aml_load_from_acpi(void){
    g_amlt_n=0; u64 rsdp=acpi_find_rsdp(); if(!rsdp)return;
    const u8* r=(const u8*)rsdp; u64 sdt=0; int w=4; if(r[15]>=2&&acpi_r32(r+20)>=36&&acpi_r64(r+24)){ sdt=acpi_r64(r+24); w=8; } else sdt=acpi_r32(r+16);
    const u8* x=acpi_tbl_ok(sdt); if(!x)return;
    u32 n=(acpi_r32(x+4)-36)/(u32)w;
    for(u32 i=0;i<n;i++){ u64 e=w==8?acpi_r64(x+36+i*8):acpi_r32(x+36+i*4); const u8* t=acpi_tbl_ok(e); if(!t)continue;
        if(t[0]=='F'&&t[1]=='A'&&t[2]=='C'&&t[3]=='P'){ u32 fl=acpi_r32(t+4); u64 d=0; if(fl>=148)d=acpi_r64(t+140); if(!d)d=acpi_r32(t+40); const u8* ds=acpi_tbl_ok(d);
            if(ds&&ds[0]=='D'&&ds[1]=='S'&&ds[2]=='D'&&ds[3]=='T'&&g_amlt_n<AMLT_MAX){ g_amlt[g_amlt_n]=ds+36; g_amll[g_amlt_n]=acpi_r32(ds+4)-36; g_amlt_n++; } }
        else if(t[0]=='S'&&t[1]=='S'&&t[2]=='D'&&t[3]=='T'&&g_amlt_n<AMLT_MAX){ g_amlt[g_amlt_n]=t+36; g_amll[g_amlt_n]=acpi_r32(t+4)-36; g_amlt_n++; } } }
