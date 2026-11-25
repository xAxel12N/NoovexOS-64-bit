#include <stdio.h>
#include <string.h>
static char fsn[16][32], fsdat[16][8]; static int fsc=0, bad=0;
void klog(const char* s){ (void)s; }
int fs_find_name(const char* n){ for(int i=0;i<fsc;i++)if(!strcmp(fsn[i],n))return i; return -1; }
char* fsd(int k){ return fsdat[k]; }
int fs_save_bin(const char* n,const unsigned char* b,int l,int x){ (void)x; int k=fs_find_name(n); if(k<0){ k=fsc++; strcpy(fsn[k],n); } memcpy(fsdat[k],b,l); return 0; }
void nvx3_sync_all(void){}
int pci_n=0; static unsigned pci_dev_ids[4]; static int pci_ndev=0;
unsigned pci_cfg(int b,int s,int f,int off){ if(b||f||s>=pci_ndev)return off?0:0xFFFFFFFFu; if(off==0)return pci_dev_ids[s]; if(off==8)return 0x03000000u; return 0; }
#include "drivers/video/nvidia/nvsettings.c"
#define T(c) do{ if(!(c)){ printf("FAIL %s line %d\n",#c,__LINE__); bad++; } }while(0)
static void reboot(void){ nvse_loaded=0; for(int f=0;f<NVSET_COUNT;f++){ NVSE[f].on=1; NVSE[f].tried=0; NVSE[f].fails=0; NVSE[f].off=0; NVSE[f].skip=0; NVSE[f].st=0; NVSE[f].err[0]=0; } }
int main(void){
    for(int f=0;f<NVSET_COUNT;f++){ T(nvset_get(f)==1); T(nvset_allowed(f)==1); }
    T(nvset_find("modeset")==NVSET_MODESET); T(nvset_find("nope")<0); T(nvset_get(99)==0); T(nvset_set(99,1)<0);
    T(nvset_try_begin(NVSET_MODESET)); reboot(); T(!nvset_allowed(NVSET_MODESET)); T(nvset_state(NVSET_MODESET)==NVSET_ST_AUTO_OFF); T(nvset_last_error(NVSET_MODESET)[0]);
    reboot(); T(!nvset_allowed(NVSET_MODESET)); nvset_set(NVSET_MODESET,1); T(nvset_allowed(NVSET_MODESET)); T(nvset_fails(NVSET_MODESET)==0);
    for(int i=0;i<NVSET_MAX_FAILS;i++){ T(nvset_try_begin(NVSET_CURSOR)); nvset_try_end(NVSET_CURSOR,0,"x"); }
    T(nvset_auto_off(NVSET_CURSOR)); reboot(); T(!nvset_allowed(NVSET_CURSOR)); nvset_reset(NVSET_CURSOR); T(nvset_allowed(NVSET_CURSOR));
    T(nvset_try_begin(NVSET_2D)); nvset_try_end(NVSET_2D,1,0); T(nvset_state(NVSET_2D)==NVSET_ST_ACTIVE); reboot(); T(nvset_allowed(NVSET_2D));
    nvset_set(NVSET_ADA,0); reboot(); T(!nvset_get(NVSET_ADA)); T(!nvset_try_begin(NVSET_ADA)); T(!strcmp(nvset_state_text(NVSET_ADA),"off"));
    char b[1400]; T(nvset_cmd("",b,sizeof b)==0); T(strstr(b,"bootstart")!=0); T(nvset_cmd("on ada",b,sizeof b)==0); T(nvset_get(NVSET_ADA));
    T(nvset_no_card()==0); pci_n=1; pci_ndev=1; pci_dev_ids[0]=0x46A68086u; T(nvset_no_card()==1); T(nvset_state(NVSET_CURSOR)==NVSET_ST_NO_HW); T(strstr(nvset_state_text(NVSET_CURSOR),"no NVIDIA graphics card")!=0);
    T(nvset_state(NVSET_EXTEND)!=NVSET_ST_NO_HW); T(nvset_cmd("",b,sizeof b)==0); T(strstr(b,"cursor    on - no NVIDIA graphics card")!=0); nvset_set(NVSET_ADA,0); T(nvset_state(NVSET_ADA)==NVSET_ST_OFF);
    nvse_card=-1; pci_dev_ids[0]=0x1F0610DEu; T(nvset_no_card()==0); T(nvset_state(NVSET_CURSOR)!=NVSET_ST_NO_HW);
    if(!bad)printf("ALL PASS\n"); return bad?1:0; }
