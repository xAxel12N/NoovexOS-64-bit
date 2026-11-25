#include "include/nvx/base.h"
#include "include/nvx/drivers/usb/usbreg.h"

usbreg_t g_usbreg[USBREG_MAX]; u32 g_usbreg_gen=0;
extern volatile u64 g_ticks;

typedef struct { char* o; char* e; } ureg_w;
static void ureg_c(ureg_w* w,char c){ if(w->o<w->e)*w->o++=c; }
static void ureg_s(ureg_w* w,const char* s){ while(s&&*s)ureg_c(w,*s++); }
static void ureg_u(ureg_w* w,u32 v,int mind){ char b[12]; int n=0; do{ b[n++]=(char)('0'+v%10); v/=10; }while(v&&n<11); while(n<mind&&n<11)b[n++]='0'; while(n)ureg_c(w,b[--n]); }
static void ureg_x(ureg_w* w,u32 v,int dg){ static const char H[]="0123456789abcdef"; for(int i=dg-1;i>=0;i--)ureg_c(w,H[(v>>(4*i))&15]); }
static int ureg_end(ureg_w* w,char* base){ if(w->o>=w->e)w->o=w->e; *w->o=0; return (int)(w->o-base); }
static void ureg_cpy(char* d,const char* s,int cap){ int i=0; for(;s&&s[i]&&i<cap-1;i++)d[i]=s[i]; d[i]=0; }
static u32 ureg_mask(int depth){ return depth<=0?0u:depth>=5?0xFFFFFu:((1u<<(4*depth))-1u); }

usbreg_t* usbreg_find(int ctl,int root,u32 route,int depth){
    route&=ureg_mask(depth);
    for(int i=0;i<USBREG_MAX;i++){ usbreg_t* r=&g_usbreg[i]; if(r->used&&r->ctl==ctl&&r->root==root&&r->depth==depth&&r->route==route)return r; }
    return 0; }
static int ureg_devnum(int ctl){ for(int n=2;n<128;n++){ int used=0; for(int i=0;i<USBREG_MAX;i++) if(g_usbreg[i].used&&g_usbreg[i].ctl==ctl&&g_usbreg[i].devnum==n){ used=1; break; } if(!used)return n; } return 127; }
usbreg_t* usbreg_add(int ctl,int root,u32 route,int depth,int speed,const u8* dd,const u8* cfg,int cfgn){
    if(ctl<0||ctl>15||root<0||root>254||depth<0||depth>5||!dd||dd[0]<18||dd[1]!=1)return 0;
    route&=ureg_mask(depth);
    usbreg_t* r=usbreg_find(ctl,root,route,depth); int fresh=0;
    if(r&&(r->vid!=(u16)(dd[8]|(dd[9]<<8))||r->pid!=(u16)(dd[10]|(dd[11]<<8)))){ r->mfr[0]=0; r->prod[0]=0; r->ser[0]=0; }
    if(!r){ for(int i=0;i<USBREG_MAX;i++) if(!g_usbreg[i].used){ r=&g_usbreg[i]; break; } if(!r)return 0;
        for(u32 i=0;i<sizeof(*r);i++)((u8*)r)[i]=0; fresh=1; }
    r->ctl=(u8)ctl; r->root=(u8)root; r->route=route; r->depth=(u8)depth; r->speed=(u8)speed;
    for(int i=0;i<18;i++)r->dd[i]=dd[i];
    r->bcdusb=(u16)(dd[2]|(dd[3]<<8)); r->vid=(u16)(dd[8]|(dd[9]<<8)); r->pid=(u16)(dd[10]|(dd[11]<<8)); r->bcd=(u16)(dd[12]|(dd[13]<<8));
    if(cfg&&cfgn>=9&&cfg[1]==2){ int n=cfgn>USBREG_CFG?USBREG_CFG:cfgn; for(int i=0;i<n;i++)r->cfg[i]=cfg[i]; r->cfgn=(u16)n; r->cfgval=cfg[5]; r->ma=(u16)(cfg[8]*(speed>=4?8:2));
        r->nif=0; int f=0; for(int i=0;i+1<n;){ int bl=cfg[i]; if(bl<2)break; if(cfg[i+1]==4&&bl>=9&&i+7<n&&cfg[i+3]==0){ if(r->nif<8)r->ifcls[r->nif]=cfg[i+5]; r->nif++; if(!f){ f=1; r->sub=cfg[i+6]; r->proto=cfg[i+7]; } } i+=bl; } }
    else { r->cfgn=0; r->nif=0; r->ma=0; r->cfgval=0; }
    r->cls=dd[4]; if((!r->cls||r->cls==0xEF)&&r->nif)r->cls=r->ifcls[0];
    if(fresh){ r->devnum=(u8)ureg_devnum(ctl); r->t=g_ticks; }
    r->used=1; g_usbreg_gen++; return r; }
void usbreg_strings(usbreg_t* r,const char* mfr,const char* prod,const char* ser){
    if(!r)return; if(mfr)ureg_cpy(r->mfr,mfr,32); if(prod)ureg_cpy(r->prod,prod,40); if(ser)ureg_cpy(r->ser,ser,32); g_usbreg_gen++; }
void usbreg_gone(int ctl,int root,u32 R,int dR){
    u32 m=ureg_mask(dR);
    for(int i=0;i<USBREG_MAX;i++){ usbreg_t* r=&g_usbreg[i]; if(!r->used||r->ctl!=ctl||r->root!=root||r->depth<dR)continue; if((r->route&m)!=(R&m))continue; r->used=0; g_usbreg_gen++; } }
int usbreg_count(void){ int n=0; for(int i=0;i<USBREG_MAX;i++) if(g_usbreg[i].used)n++; return n; }
int usbreg_tier_port(const usbreg_t* r,int tier){ if(!r||tier<0||tier>=r->depth)return 0; return (int)((r->route>>(4*tier))&15u); }
int usbreg_parent(int i){
    if(i<0||i>=USBREG_MAX||!g_usbreg[i].used||!g_usbreg[i].depth)return -1;
    usbreg_t* c=&g_usbreg[i]; usbreg_t* p=usbreg_find(c->ctl,c->root,c->route,c->depth-1);
    return p?(int)(p-g_usbreg):-1; }
int usbreg_bus_name(const usbreg_t* r,char* out,int cap){
    ureg_w w={out,out+cap-1}; if(cap<2)return 0;
    ureg_u(&w,(u32)r->ctl+1,0); ureg_c(&w,'-'); ureg_u(&w,(u32)r->root+1,0);
    for(int t=0;t<r->depth;t++){ ureg_c(&w,'.'); ureg_u(&w,(u32)usbreg_tier_port(r,t),0); }
    return ureg_end(&w,out); }
const char* usbreg_speed_str(int s){ switch(s){ case 1: return "12M"; case 2: return "1.5M"; case 3: return "480M"; case 4: return "5000M"; case 5: return "10000M"; case 6: return "20000M"; } return "?"; }
int usbreg_speed_mbps(int s){ switch(s){ case 1: return 12; case 2: return 1; case 3: return 480; case 4: return 5000; case 5: return 10000; case 6: return 20000; } return 0; }
const char* usbreg_class_name(int c){ switch(c){ case 0: return "[unknown]"; case 1: return "Audio"; case 2: return "Communications"; case 3: return "Human Interface Device"; case 5: return "Physical Interface Device"; case 6: return "Image"; case 7: return "Printer"; case 8: return "Mass Storage"; case 9: return "Hub"; case 0x0A: return "CDC Data"; case 0x0B: return "Chip/SmartCard"; case 0x0D: return "Content Security"; case 0x0E: return "Video"; case 0x0F: return "Personal Healthcare"; case 0x10: return "Audio/Video"; case 0x11: return "Billboard"; case 0xDC: return "Diagnostic"; case 0xE0: return "Wireless"; case 0xEF: return "Miscellaneous Device"; case 0xFE: return "Application Specific Interface"; case 0xFF: return "Vendor Specific Class"; } return "[unknown]"; }
const char* usbreg_driver(const usbreg_t* r){ if(!r)return "[none]"; const char* d=usbreg_drv_at(r->ctl,r->root,r->route,r->depth); return d&&d[0]?d:"[none]"; }
int usbreg_line(const usbreg_t* r,char* out,int cap){
    ureg_w w={out,out+cap-1}; if(cap<2)return 0;
    ureg_s(&w,"Bus "); ureg_u(&w,(u32)r->ctl+1,3); ureg_s(&w," Device "); ureg_u(&w,r->devnum,3); ureg_s(&w,": ID "); ureg_x(&w,r->vid,4); ureg_c(&w,':'); ureg_x(&w,r->pid,4);
    if(r->mfr[0]){ ureg_c(&w,' '); ureg_s(&w,r->mfr); }
    if(r->prod[0]){ ureg_c(&w,' '); ureg_s(&w,r->prod); }
    if(!r->mfr[0]&&!r->prod[0]){ ureg_c(&w,' '); ureg_s(&w,usbreg_class_name(r->cls)); }
    return ureg_end(&w,out); }
int usbreg_list(char* out,int cap){
    ureg_w w={out,out+cap-1}; if(cap<2)return 0;
    int nh=usbreg_hc_count(); if(nh>16)nh=16;
    for(int c=nh-1;c>=0;c--){
        int ord[USBREG_MAX], n=0; for(int i=0;i<USBREG_MAX;i++) if(g_usbreg[i].used&&g_usbreg[i].ctl==c)ord[n++]=i;
        for(int a=1;a<n;a++){ int v=ord[a], b=a-1; while(b>=0&&g_usbreg[ord[b]].devnum<g_usbreg[v].devnum){ ord[b+1]=ord[b]; b--; } ord[b+1]=v; }
        for(int k=0;k<n;k++){ char l[128]; usbreg_line(&g_usbreg[ord[k]],l,sizeof l); ureg_s(&w,l); ureg_c(&w,'\n'); }
        ureg_s(&w,"Bus "); ureg_u(&w,(u32)c+1,3); ureg_s(&w," Device 001: ID 1d6b:0003 Linux Foundation 3.0 root hub\n"); }
    return ureg_end(&w,out); }
static void ureg_node(ureg_w* w,int i,int lvl){
    usbreg_t* r=&g_usbreg[i]; int port=r->depth?usbreg_tier_port(r,r->depth-1):r->root+1;
    for(int s=0;s<lvl;s++)ureg_s(w,"    "); ureg_s(w,"|__ Port "); ureg_u(w,(u32)port,3); ureg_s(w,": Dev "); ureg_u(w,r->devnum,3); ureg_s(w,", ID "); ureg_x(w,r->vid,4); ureg_c(w,':'); ureg_x(w,r->pid,4);
    if(r->prod[0]){ ureg_c(w,' '); ureg_s(w,r->prod); }
    ureg_s(w,", Class="); ureg_s(w,usbreg_class_name(r->cls)); ureg_s(w,", Driver="); ureg_s(w,usbreg_driver(r)); ureg_s(w,", "); ureg_s(w,usbreg_speed_str(r->speed));
    if(r->cfgn){ ureg_s(w,", "); if(r->cfg[7]&0x40)ureg_s(w,"self-powered "); ureg_u(w,r->ma,0); ureg_s(w,"mA"); }
    ureg_c(w,'\n');
    if(lvl>=6)return;
    for(int p=1;p<=15;p++) for(int k=0;k<USBREG_MAX;k++){ usbreg_t* c=&g_usbreg[k]; if(!c->used||c->ctl!=r->ctl||c->root!=r->root||c->depth!=r->depth+1)continue; if((c->route&ureg_mask(r->depth))!=r->route||usbreg_tier_port(c,r->depth)!=p)continue; ureg_node(w,k,lvl+1); } }
int usbreg_tree(char* out,int cap){
    ureg_w w={out,out+cap-1}; if(cap<2)return 0;
    int nh=usbreg_hc_count(); if(nh>16)nh=16;
    for(int c=0;c<nh;c++){
        int np=usbreg_hc_ports(c); ureg_s(&w,"/:  Bus "); ureg_u(&w,(u32)c+1,3); ureg_s(&w,".Port 001: Dev 001, Class=root_hub, Driver=xhci_hcd/"); ureg_u(&w,(u32)(np>0?np:0),0); ureg_s(&w,"p\n");
        for(int p=0;p<255;p++) for(int k=0;k<USBREG_MAX;k++){ usbreg_t* r=&g_usbreg[k]; if(r->used&&r->ctl==c&&r->root==p&&!r->depth)ureg_node(&w,k,1); } }
    return ureg_end(&w,out); }
const char* usbreg_kind(const usbreg_t* r){ if(!r)return "USB device"; int c=r->nif?r->ifcls[0]:r->cls;
    if(c==3){ int k=0,m=0; for(int i=0;i+8<r->cfgn;){ int bl=r->cfg[i]; if(bl<2)break; if(r->cfg[i+1]==4&&r->cfg[i+5]==3){ if(r->cfg[i+7]==1)k=1; else if(r->cfg[i+7]==2)m=1; } i+=bl; } return k&&m?"Keyboard and mouse":k?"Keyboard":m?"Mouse":"Input device"; }
    switch(r->cls){ case 1: return "Audio device"; case 2: return "Network adapter"; case 6: return "Camera (still image)"; case 7: return "Printer"; case 8: return "USB drive"; case 9: return "USB hub"; case 0x0B: return "Card reader"; case 0x0E: return "Camera"; case 0xE0: return "Bluetooth adapter"; case 0xFF: return "Vendor device"; }
    return "USB device"; }
int usbreg_card(int i,char* out,int cap){
    ureg_w w={out,out+cap-1}; if(cap<2||i<0||i>=USBREG_MAX||!g_usbreg[i].used){ if(cap>0)out[0]=0; return 0; }
    usbreg_t* r=&g_usbreg[i]; char bn[24]; usbreg_bus_name(r,bn,sizeof bn);
    ureg_s(&w,"Port "); ureg_s(&w,bn); ureg_s(&w,"  "); ureg_s(&w,usbreg_speed_str(r->speed)); ureg_s(&w,"bit/s  ");
    if(r->cfgn){ ureg_u(&w,r->ma,0); ureg_s(&w,r->cfg[7]&0x40?" mA, self-powered  ":" mA  "); } ureg_s(&w,usbreg_driver(r)); ureg_s(&w,"  "); ureg_x(&w,r->vid,4); ureg_c(&w,':'); ureg_x(&w,r->pid,4);
    return ureg_end(&w,out); }
static void ureg_ord(int i,int* idx,int max,int* n,int lvl){ if(*n>=max)return; idx[(*n)++]=i; usbreg_t* r=&g_usbreg[i]; if(lvl>=6)return;
    for(int p=1;p<=15;p++) for(int k=0;k<USBREG_MAX;k++){ usbreg_t* c=&g_usbreg[k]; if(c->used&&c->ctl==r->ctl&&c->root==r->root&&c->depth==r->depth+1&&(c->route&ureg_mask(r->depth))==r->route&&usbreg_tier_port(c,r->depth)==p)ureg_ord(k,idx,max,n,lvl+1); } }
int usbreg_order(int* idx,int max){ int n=0;
    for(int c=0;c<16;c++) for(int p=0;p<255;p++) for(int k=0;k<USBREG_MAX;k++){ usbreg_t* r=&g_usbreg[k]; if(r->used&&r->ctl==c&&r->root==p&&!r->depth)ureg_ord(k,idx,max,&n,0); }
    return n; }
