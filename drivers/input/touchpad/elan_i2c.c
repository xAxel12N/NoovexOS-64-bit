#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/drivers/input/input.h"
#include "include/nvx/drivers/i2c/dw_i2c.h"
#include "include/nvx/drivers/input/touchpad/tp_gesture.h"
#include "include/nvx/drivers/input/touchpad/i2c_hid.h"
#include "include/nvx/drivers/input/touchpad/elan_i2c.h"
static const char* const ELAN_ACPI_IDS[]={"ELAN0000","ELAN0100","ELAN0600","ELAN0601","ELAN0602","ELAN0603","ELAN0604","ELAN0605","ELAN0606","ELAN0607","ELAN0608","ELAN0609","ELAN060B","ELAN060C","ELAN060F",
    "ELAN0610","ELAN0611","ELAN0612","ELAN0615","ELAN0616","ELAN0617","ELAN0618","ELAN0619","ELAN061A","ELAN061C","ELAN061D","ELAN061E","ELAN061F","ELAN0620","ELAN0621","ELAN0622","ELAN0623",
    "ELAN0624","ELAN0625","ELAN0626","ELAN0627","ELAN0628","ELAN0629","ELAN062A","ELAN062B","ELAN062C","ELAN062D","ELAN062E","ELAN062F","ELAN0631","ELAN0632","ELAN0633","ELAN0634","ELAN0635",
    "ELAN0636","ELAN0637","ELAN1000"};
#define EL_REG_DESC 0x0001
#define EL_REG_RDESC 0x0002
#define EL_REG_STAND 0x0005
#define EL_REG_PATTERN 0x0100
#define EL_REG_PRODUCT 0x0101
#define EL_REG_FWVER 0x0102
#define EL_REG_ICTYPE 0x0103
#define EL_REG_SMVER 0x0104
#define EL_REG_TRACES 0x0105
#define EL_REG_MAXX 0x0106
#define EL_REG_MAXY 0x0107
#define EL_REG_RES 0x0108
#define EL_REG_SET 0x0300
#define EL_REG_POWER 0x0307
#define EL_RESET 0x0100
#define EL_WAKE 0x0800
#define EL_SLEEP 0x0801
#define EL_ABS 0x0001
#define EL_RID_LO 0x5D
#define EL_RID_TP 0x5E
#define EL_RID_TP2 0x5F
#define EL_RID_HI 0x60
int elan_acpi_match(const char* h){ if(h[0]=='E'&&h[1]=='L'&&h[2]=='A'&&h[3]=='N'&&h[4]=='0'&&h[5]=='6'&&h[6]&&h[7]&&!h[8])return 1; for(u32 i=0;i<sizeof(ELAN_ACPI_IDS)/sizeof(ELAN_ACPI_IDS[0]);i++){ const char* a=ELAN_ACPI_IDS[i]; int k=0; while(a[k]&&a[k]==h[k])k++; if(!a[k]&&!h[k])return 1; } return 0; }
static int el_rd(tpdev* t,u16 reg,u8* v,int n){ u8 w[2]={(u8)reg,(u8)(reg>>8)}; return dw_xfer(&t->bus,t->addr,w,2,v,n); }
static int el_wr(tpdev* t,u16 reg,u16 val){ u8 w[4]={(u8)reg,(u8)(reg>>8),(u8)val,(u8)(val>>8)}; return dw_xfer(&t->bus,t->addr,w,4,0,0); }
static int el_res_upmm(int pattern,int raw){ int s=(int)(int8_t)(u8)raw; int dpi=pattern<=1?s*10+790:(s+3)*100; if(dpi<100)dpi=100; int u=dpi*10/254; return u<1?1:u; }
int el_setup(tpdev* t){
    u8 v[4];
    t->stage=11; if(el_wr(t,EL_REG_STAND,EL_RESET))return -1;
    ih_delay_ms(100); { u8 a[2]; ih_read_input(t,a,2); }
    u8 d[30]; for(int i=0;i<30;i++)d[i]=0xFF; if(el_rd(t,EL_REG_DESC,d,30))return -2;
    { int ff=1; for(int i=0;i<30;i++) if(d[i]!=0xFF){ ff=0; break; } if(ff)return -2; }
    if(!t->rdesc)t->rdesc=(u8*)kmalloc(4096); if(t->rdesc)el_rd(t,EL_REG_RDESC,t->rdesc,158);
    t->stage=12;
    if(el_rd(t,EL_REG_PATTERN,v,2))return -3; t->el_pattern=(v[0]==0xFF&&v[1]==0xFF)?0:v[1];
    if(el_rd(t,EL_REG_PRODUCT,v,2))return -3; t->el_product=(u16)(v[0]|(v[1]<<8));
    if(el_rd(t,EL_REG_FWVER,v,2)==0)t->el_fw=v[0];
    if(t->el_pattern>=1){ if(el_rd(t,EL_REG_ICTYPE,v,2)==0)t->el_ic=(u16)((v[0]<<8)|v[1]); if(el_rd(t,EL_REG_SMVER,v,2)==0)t->el_clickpad=(v[0]&0x10)!=0; }
    else { if(el_rd(t,0x0110,v,2)==0)t->el_ic=v[0]; if(el_rd(t,EL_REG_SMVER,v,2)==0)t->el_clickpad=(v[0]&0x10)!=0; }
    if(el_rd(t,EL_REG_MAXX,v,2))return -4; t->el_maxx=(int)(v[0]|(v[1]<<8));
    if(el_rd(t,EL_REG_MAXY,v,2))return -4; t->el_maxy=(int)(v[0]|(v[1]<<8));
    if(t->el_maxx<16||t->el_maxy<16||t->el_maxx>0x7FFF||t->el_maxy>0x7FFF)return -4;
    int ux=15, uy=15; if(el_rd(t,EL_REG_RES,v,2)==0){ ux=el_res_upmm(t->el_pattern,v[0]); uy=el_res_upmm(t->el_pattern,v[1]); }
    t->el_replen=t->el_pattern<=1?34:39;
    t->stage=13;
    if(el_wr(t,EL_REG_SET,EL_ABS))return -5;
    el_wr(t,EL_REG_STAND,EL_WAKE);
    if(el_rd(t,EL_REG_POWER,v,2)==0){ u16 pw=(u16)(v[0]|(v[1]<<8)); if(pw&1)el_wr(t,EL_REG_POWER,(u16)(pw&~1u)); }
    t->d.maxin=(u16)t->el_replen; t->d.vid=0x04F3; t->d.pid=t->el_product;
    tpe_init(&t->eng,0,t->el_maxx,0,t->el_maxy,(u32)((long long)t->el_maxx*1000/ux),(u32)((long long)t->el_maxy*1000/uy));
    t->lay.nfing=5; t->lay.xmax=t->el_maxx; t->lay.ymax=t->el_maxy; t->lay.xum=(u32)((long long)t->el_maxx*1000/ux); t->lay.yum=(u32)((long long)t->el_maxy*1000/uy); t->lay.btn[0].ok=t->el_clickpad;
    t->proto=1; t->mode=1; t->stage=14; t->ok=1; return 0; }
void el_process(tpdev* t,const u8* b,int n){
    int len=b[0]|(b[1]<<8); if(len==0){ t->empty++; return; } if(n<5)return;
    int rid=b[2];
    if(rid==EL_RID_TP||rid==EL_RID_TP2){ const u8* p=b+3; if(n<9)return;
        int x=0,y=0; if((p[3]&0x0F)==6){ x=(int)p[4]-(int)(((p[1]^0x80)&0xFF)<<1); y=(int)(((p[2]^0x80)&0xFF)<<1)-(int)p[5]; }
        in_rel(x,-y); in_buttons(p[0]&1,(p[0]>>1)&1,(p[0]>>2)&1); t->el_tp_pkts++; return; }
    if(rid!=EL_RID_LO&&rid!=EL_RID_HI)return;
    int hp=rid==EL_RID_HI; if(n<(hp?34:30))return;
    int touch=b[3]; const u8* f=b+4; tpcontact cs[5]; int cnt=0;
    for(int i=0;i<5;i++){ if(!(touch&(1<<(3+i))))continue; if(f+5>b+n)break;
        int x,y; if(hp){ x=(f[0]<<8)|f[1]; y=(f[2]<<8)|f[3]; } else { x=((f[0]&0xF0)<<4)|f[1]; y=((f[0]&0x0F)<<8)|f[2]; }
        f+=5; if(x>t->el_maxx||y>t->el_maxy)continue;
        cs[cnt].id=i; cs[cnt].x=x; cs[cnt].y=t->el_maxy-y; cnt++; }
    int btns=touch&7;
    if(t->el_clickpad){ ih_frame(t,cs,cnt,btns!=0); }
    else { ih_frame(t,cs,cnt,0); if(btns!=t->el_lastbtn)in_buttons(btns&1,(btns>>1)&1,(btns>>2)&1); }
    t->el_lastbtn=btns; }
