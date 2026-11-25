#include "include/nvx/base.h"
#include "include/nvx/drivers/video/nvidia/nvsettings.h"

extern void klog(const char*);
extern int fs_find_name(const char*);
extern char* fsd(int);
extern int fs_save_bin(const char*,const u8*,int,int);
extern void nvx3_sync_all(void);
extern int g_nv_ampere_on __attribute__((weak));
extern int g_nv_ada_on __attribute__((weak));
extern int g_nvcur_enable __attribute__((weak));
extern int g_nvd_vram __attribute__((weak));
extern int g_nvd_2d __attribute__((weak));
extern void nvd_set_auto(int) __attribute__((weak));
extern int nvflip_setting(int) __attribute__((weak));
extern int nvcur_cmd(const char*,char*,int) __attribute__((weak));
extern int nvgsp_cmd(const char*) __attribute__((weak));
extern int display_extend_apply(int) __attribute__((weak));
extern void nvc_sm8x_setting(int) __attribute__((weak));
extern int pci_n __attribute__((weak));
extern u32 pci_cfg(int,int,int,int) __attribute__((weak));

typedef struct{ const char* name; const char* label; char on, tried, fails, off, skip, st, loaded; char err[72]; } nvset_t;
static nvset_t NVSE[NVSET_COUNT]={
    {"bootstart","Start the GPU during the boot splash",1,0,0,0,0,0,0,{0}},
    {"modeset","Native mode and tear-free page flips",1,0,0,0,0,0,0,{0}},
    {"cursor","Hardware mouse cursor",1,0,0,0,0,0,0,{0}},
    {"extend","Extended desktop on the second screen",1,0,0,0,0,0,0,{0}},
    {"cardmem","Window layers in video memory",1,0,0,0,0,0,0,{0}},
    {"2d","2D engine fills and copies",1,0,0,0,0,0,0,{0}},
    {"dptrain","DisplayPort link training during mode set",1,0,0,0,0,0,0,{0}},
    {"ampere","RTX 30 (Ampere) GSP start",1,0,0,0,0,0,0,{0}},
    {"ada","RTX 40 (Ada) GSP start",1,0,0,0,0,0,0,{0}},
    {"shader8x","RTX 30/40 compute compositor",1,0,0,0,0,0,0,{0}},
};
static int nvse_loaded=0;

static int nvse_ok(int f){ return f>=0&&f<NVSET_COUNT; }
static char* nvse_cp(char* d,const char* s,char* e){ while(s&&*s&&d<e)*d++=*s++; return d; }
static void nvse_file(int f,char* b){ char* e=b+30; b=nvse_cp(b,"nvset-",e); b=nvse_cp(b,NVSE[f].name,e); b=nvse_cp(b,".cfg",e); *b=0; }
static void nvse_save(int f){ char nm[32]; nvse_file(f,nm); nvset_t* s=&NVSE[f];
    u8 b[5]={(u8)(s->on?'1':'0'),(u8)(s->tried?'1':'0'),(u8)('0'+(s->fails>9?9:s->fails)),(u8)(s->off?'1':'0'),'\n'};
    fs_save_bin(nm,b,5,0); nvx3_sync_all(); }
static void nvse_note(int f,const char* a,const char* b){ char m[160],*p=m,*e=m+158; p=nvse_cp(p,"nvset: ",e); p=nvse_cp(p,NVSE[f].name,e); p=nvse_cp(p," - ",e); p=nvse_cp(p,a,e); p=nvse_cp(p,b,e); *p=0; klog(m); }
static void nvse_err(int f,const char* s){ char* p=NVSE[f].err; p=nvse_cp(p,s?s:"",NVSE[f].err+71); *p=0; }

static void nvse_apply_globals(void){
    if(&g_nv_ampere_on)g_nv_ampere_on=nvset_allowed(NVSET_AMPERE);
    if(&g_nv_ada_on)g_nv_ada_on=nvset_allowed(NVSET_ADA);
    if(&g_nvcur_enable)g_nvcur_enable=nvset_allowed(NVSET_CURSOR);
    if(&g_nvd_vram)g_nvd_vram=nvset_allowed(NVSET_CARDMEM);
    if(&g_nvd_2d&&!nvset_allowed(NVSET_2D))g_nvd_2d=0; }

void nvset_load(void){
    if(nvse_loaded){ return; } nvse_loaded=1;
    for(int f=0;f<NVSET_COUNT;f++){ nvset_t* s=&NVSE[f]; char nm[32]; nvse_file(f,nm); int k=fs_find_name(nm); char* d=k>=0?fsd(k):0;
        s->loaded=1;
        if(!d)continue;
        s->on=d[0]!='0'; s->tried=d[1]=='1'; s->fails=(d[2]>='0'&&d[2]<='9')?(char)(d[2]-'0'):0; s->off=d[3]=='1';
        if(s->tried){ s->tried=0; s->off=1; s->skip=1; if(s->fails<NVSET_MAX_FAILS)s->fails++; nvse_err(f,"the last try did not finish (the computer hung or restarted)");
            nvse_note(f,"did not finish last time, turned off automatically ","(Settings or `gpu feature on <name>` to try again)"); nvse_save(f); }
        else if(s->off)nvse_note(f,"stays off after earlier failures: ",s->err[0]?s->err:"turned off automatically"); }
    nvse_apply_globals(); }

int nvset_count(void){ return NVSET_COUNT; }
const char* nvset_name(int f){ return nvse_ok(f)?NVSE[f].name:""; }
const char* nvset_label(int f){ return nvse_ok(f)?NVSE[f].label:""; }
int nvset_find(const char* n){ for(int f=0;f<NVSET_COUNT;f++){ const char* a=NVSE[f].name; int i=0; while(a[i]&&n[i]==a[i])i++; if(!a[i]&&(!n[i]||n[i]==' '))return f; } return -1; }
int nvset_get(int f){ if(!nvse_ok(f))return 0; nvset_load(); return NVSE[f].on; }
int nvset_allowed(int f){ if(!nvse_ok(f))return 0; nvset_load(); return NVSE[f].on&&!NVSE[f].off&&!NVSE[f].skip; }
static int nvse_card=-1;
int nvset_no_card(void){ if(nvse_card>=0)return !nvse_card; if(!pci_cfg||!&pci_n||pci_n<=0)return 0; int any=0;
    for(int b=0;b<256;b++) for(int s=0;s<32;s++){ u32 id0=pci_cfg(b,s,0,0); if((id0&0xFFFF)==0xFFFF||!(id0&0xFFFF))continue; any=1; int nf=(pci_cfg(b,s,0,0x0C)&0x800000)?8:1;
        for(int fn=0;fn<nf;fn++){ u32 id=pci_cfg(b,s,fn,0); if((id&0xFFFF)==0x10DE&&((pci_cfg(b,s,fn,0x08)>>24)&0xFF)==3){ nvse_card=1; return 0; } } }
    nvse_card=any?0:-1; return any; }
int nvset_state(int f){ if(!nvse_ok(f))return NVSET_ST_OFF; nvset_load(); nvset_t* s=&NVSE[f];
    if(s->off||s->skip){ return NVSET_ST_AUTO_OFF; } if(!s->on){ return NVSET_ST_OFF; } if(f!=NVSET_EXTEND&&nvset_no_card())return NVSET_ST_NO_HW; return s->st?s->st:NVSET_ST_WAITING; }
const char* nvset_state_text(int f){
    switch(nvset_state(f)){
    case NVSET_ST_OFF: return "off";
    case NVSET_ST_WAITING: return "on - not started yet";
    case NVSET_ST_ACTIVE: return "on - working";
    case NVSET_ST_FALLBACK: return "on - not in use, fell back to the safe path";
    case NVSET_ST_AUTO_OFF: return "turned off automatically after a failure";
    case NVSET_ST_NO_HW: return f!=NVSET_EXTEND&&nvset_no_card()?"on - no NVIDIA graphics card":"on - no suitable hardware";
    } return "unknown"; }
const char* nvset_last_error(int f){ if(!nvse_ok(f))return ""; nvset_load(); return NVSE[f].err; }
int nvset_fails(int f){ if(!nvse_ok(f))return 0; nvset_load(); return NVSE[f].fails; }
int nvset_auto_off(int f){ if(!nvse_ok(f))return 0; nvset_load(); return NVSE[f].off||NVSE[f].skip; }
void nvset_reset(int f){ if(!nvse_ok(f))return; nvset_load(); nvset_t* s=&NVSE[f]; s->off=0; s->skip=0; s->fails=0; s->tried=0; s->err[0]=0; if(s->st==NVSET_ST_FALLBACK)s->st=0; nvse_save(f); nvse_apply_globals(); }

int nvset_try_begin(int f){ if(!nvset_allowed(f))return 0; NVSE[f].tried=1; nvse_save(f); return 1; }
void nvset_try_end(int f,int ok,const char* err){
    if(!nvse_ok(f)){ return; } nvset_t* s=&NVSE[f]; s->tried=0;
    if(ok){ s->st=NVSET_ST_ACTIVE; s->fails=0; s->err[0]=0; nvse_save(f); return; }
    s->st=NVSET_ST_FALLBACK; if(s->fails<9)s->fails++; nvse_err(f,err?err:"failed");
    if(s->fails>=NVSET_MAX_FAILS){ s->off=1; nvse_note(f,"failed too often, turned off automatically: ",s->err); nvse_apply_globals(); }
    nvse_save(f); }
void nvset_try_cancel(int f){ if(!nvse_ok(f)||!NVSE[f].tried)return; NVSE[f].tried=0; nvse_save(f); }
void nvset_note(int f,int st,const char* err){ if(!nvse_ok(f))return; NVSE[f].st=(char)st; if(err)nvse_err(f,err); }

int nvset_set(int f,int on){
    if(!nvse_ok(f)){ return -1; } nvset_load(); nvset_t* s=&NVSE[f]; on=on?1:0;
    s->on=(char)on; if(on){ s->off=0; s->skip=0; s->fails=0; s->err[0]=0; } s->st=0; nvse_save(f); nvse_apply_globals();
    switch(f){
    case NVSET_BOOTSTART: if(nvd_set_auto)nvd_set_auto(on); break;
    case NVSET_MODESET: if(nvflip_setting){ int r=on?nvset_try_begin(f):0; int ok=nvflip_setting(on); if(r)nvset_try_end(f,ok,"the main screen stayed on the firmware picture"); } break;
    case NVSET_CURSOR: if(nvcur_cmd){ char b[160]; int r=on?nvset_try_begin(f):0; int e=nvcur_cmd(on?"on":"off",b,160); if(r)nvset_try_end(f,!e,"the cursor channel did not start"); } break;
    case NVSET_SHADER8X: if(nvc_sm8x_setting)nvc_sm8x_setting(on); break;
    case NVSET_CARDMEM: if(nvgsp_cmd)nvgsp_cmd(on?"cardmem on":"cardmem off"); break;
    case NVSET_EXTEND: if(display_extend_apply){ int r=on?nvset_try_begin(f):0; int e=display_extend_apply(on);
        if(r&&e<0){ s->tried=0; nvse_save(f); nvset_note(f,NVSET_ST_NO_HW,"no second screen found"); } else if(r)nvset_try_end(f,e>0,"could not make screen 2 its own desktop - it mirrors screen 1"); } break;
    default: break; }
    return 0; }

int nvset_cmd(const char* a,char* out,int max){
    char* p=out; char* e=out+max-1; while(*a==' ')a++;
    int on=a[0]=='o'&&a[1]=='n'&&a[2]==' ', off=a[0]=='o'&&a[1]=='f'&&a[2]=='f'&&a[3]==' ', rs=a[0]=='r'&&a[1]=='e'&&a[2]=='s'&&a[3]=='e'&&a[4]=='t'&&a[5]==' ';
    if(on||off||rs){ const char* n=a+(on?3:off?4:6); while(*n==' ')n++; int f=nvset_find(n);
        if(f<0){ p=nvse_cp(p,"gpu feature: unknown name\n",e); *p=0; return -1; }
        if(rs)nvset_reset(f); else nvset_set(f,on); }
    else if(a[0]){ p=nvse_cp(p,"gpu feature                     list the NVIDIA features and their state\ngpu feature on|off|reset <name> switch one feature (reset clears its failure count)\n",e); *p=0; return -1; }
    for(int f=0;f<NVSET_COUNT;f++){ p=nvse_cp(p,"  ",e); const char* n=NVSE[f].name; int i=0; while(n[i]){ if(p<e)*p++=n[i]; i++; } for(;i<10;i++)if(p<e)*p++=' ';
        p=nvse_cp(p,nvset_state_text(f),e); if(NVSE[f].fails){ p=nvse_cp(p," (failures: ",e); if(p<e)*p++=(char)('0'+NVSE[f].fails); p=nvse_cp(p,")",e); }
        if(NVSE[f].err[0]){ p=nvse_cp(p," - ",e); p=nvse_cp(p,NVSE[f].err,e); } if(p<e)*p++='\n'; }
    *p=0; return 0; }
