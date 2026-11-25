#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
static struct pcpu g_pc;
#undef PCPU
#define PCPU (&g_pc)
#include "shell/vtcon.c"
char TCH[TRMAX][TCMAX]; u32 TCO[TRMAX][TCMAX]; u32 TBG[TRMAX][TCMAX]; char TSB[TSB_N][TCMAX]; u32 TSBC[TSB_N][TCMAX];
int trows=30,tcols=100,tcx,tcy,tsb_head,tsb_n,tlen,g_tsb_off,g_tty_fg,g_state=ST_DESKTOP,g_locked; u32 tcur; char tline[256]; volatile unsigned g_term_gen;
u32 PITCHB, W=1024, H=768, SW=1024; volatile unsigned char kdown[128]; char g_hostname[]="box";
static int fail_n=0, fail_after=-1, inval=0, geom=0;
void* kmalloc(unsigned long n){ if(fail_after>=0&&fail_n++>=fail_after)return 0; return malloc(n); }
void kfree(void* p){ free(p); }
void klog(const char* s){ (void)s; }
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }
void term_clear(void){ memset(TCH,' ',sizeof TCH); tcx=tcy=0; tsb_n=0; }
void term_putc(char c){ if(c=='\n'){ tcx=0; tcy++; return; } if(tcy<TRMAX&&tcx<TCMAX)TCH[tcy][tcx++]=c; }
void term_puts(const char* s){ while(*s)term_putc(*s++); }
void tcol(u32 c){ tcur=c; }
void term_prompt(void){ term_puts("$ "); }
void term_geom(void){ geom++; trows=30; tcols=100; }
void live_invalidate(void){ inval++; }
struct jproc g_jp[JMAX];
void vt_out(const char* b,int n){ for(int i=0;i<n;i++)term_putc(b[i]); }
char g_user[32]="anna"; static int sudo_n=0;
static u64 sudo_u=0;
void sudo_forget(void){ sudo_n++; sudo_u=0; }
u64 sudo_until_get(void){ return sudo_u; }
void sudo_until_set(u64 v){ sudo_u=v; }
int acct_get(const char* n,acct_info* a){ memset(a,0,sizeof *a); if(!strcmp(n,"anna")){ a->uid=1000; a->gid=1000; a->haspw=1; return 0; } if(!strcmp(n,"root")){ a->uid=0; a->gid=0; a->haspw=0; return 0; } if(!strcmp(n,"lock")){ a->uid=1002; a->gid=1002; a->haspw=1; a->locked=1; return 0; } return -1; }
int acct_check(const char* n,const char* pw){ return (!strcmp(n,"anna")&&!strcmp(pw,"pw1"))||!strcmp(n,"root")||!strcmp(n,"lock"); }
int acct_wait_secs(const char* n){ (void)n; return 0; }
static int installed=1; int lg_installed(void){ return installed; }
static void typ(const char* t){ while(*t)vtc_login_key(*t++); }
static int fails=0;
static int row_has(int r,const char* w){ char b[TCMAX+1]; memcpy(b,TCH[r],TCMAX); b[TCMAX]=0; return strstr(b,w)!=0; }
#define CK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
static void keys(int f){ memset((void*)kdown,0,sizeof kdown); kdown[0x1D]=1; kdown[0x38]=1; kdown[0x3B+f]=1; }
static void nokeys(void){ memset((void*)kdown,0,sizeof kdown); }
int main(void){
    term_clear(); term_puts("desktop-term"); g_tty_fg=0;
    nokeys(); CK(vtc_tick()==0); CK(g_vtc_active==0);
    keys(6); CK(vtc_tick()==0); CK(g_state==ST_DESKTOP);
    keys(0); CK(vtc_tick()==1); CK(g_vtc_active==1); CK(g_state==ST_LIVE); CK(row_has(0,"tty1")); CK(tcols==(1024-28)/8);
    CK(vtc_tick()==0);
    term_puts("in-one"); g_tty_fg=77;
    nokeys(); vtc_tick(); keys(1); CK(vtc_tick()==1); CK(g_vtc_active==2); CK(g_tty_fg==0); CK(row_has(0,"tty2"));
    nokeys(); vtc_tick(); keys(0); CK(vtc_tick()==1); CK(g_vtc_active==1); CK(g_tty_fg==77); CK(row_has(2,"in-one"));
    u8 o[8]; int on; CK(vtc_ioctl(0x5603,0,o,&on)==0&&on==6); { u16 v[3]; memcpy(v,o,6); CK(v[0]==1); CK(v[2]&(1<<2)); CK(v[2]&(1<<7)); CK(!(v[2]&(1<<3))); }
    CK(vtc_ioctl(0x5600,0,o,&on)==0&&on==4); { int f; memcpy(&f,o,4); CK(f==3); }
    CK(vtc_ioctl(0x5606,9,o,&on)==-6);
    g_jp[g_jcur].uid=1234; CK(vtc_ioctl(0x5606,1,o,&on)==-1); CK(vtc_ioctl(0x4B3A,1,o,&on)==-1); CK(g_vtc_kdmode==0); g_jp[g_jcur].uid=g_uid; CK(vtc_ioctl(0x4B3A,0,o,&on)==0); g_jcur=JMAX; CK(vtc_ioctl(0x5606,1,o,&on)==-1); g_jcur=0; g_jp[0].uid=0;
    CK(vtc_ioctl(0x4B3A,1,o,&on)==0); CK(g_vtc_kdmode==1); CK(vtc_ioctl(0x4B3A,5,o,&on)==-22); CK(vtc_ioctl(0x4B3B,0,o,&on)==0&&on==4);
    CK(vtc_ioctl(0x1234,0,o,&on)==-25);
    g_jcur=5; g_jp[5].pgid=77; g_pc.js_running=1; CK(vtc_divert("x",1)==0);
    nokeys(); vtc_tick(); keys(2); CK(vtc_tick()==1); CK(g_vtc_active==3);
    CK(vtc_divert("bg-out",6)==1); g_jp[5].pgid=9; CK(vtc_divert("fg",2)==0); g_jp[5].pgid=77;
    { static char big[40000]; memset(big,'z',sizeof big); CK(vtc_divert(big,sizeof big)==1); CK(g_vs[1].pn==VTC_PEND); g_vs[1].pn=0; }
    CK(vtc_divert("late",4)==1); g_pc.js_running=0;
    nokeys(); vtc_tick(); keys(0); CK(vtc_tick()==1); CK(g_vtc_active==1); CK(g_vs[1].pn==0); CK(row_has(2,"in-onelate"));
    CK(vtc_ioctl(0x5606,7,o,&on)==0); nokeys(); CK(vtc_tick()==1); CK(g_vtc_active==0); CK(g_state==ST_DESKTOP); CK(row_has(0,"desktop-term")); CK(geom>0);
    CK(vtc_ioctl(0x5607,7,o,&on)==0);
    g_locked=1; keys(0); CK(vtc_tick()==0); CK(g_vtc_active==0); CK(g_state==ST_DESKTOP); g_locked=0;
    g_state=ST_LOGIN; nokeys(); vtc_tick(); keys(0); CK(vtc_tick()==0); CK(g_vtc_active==0); g_state=ST_DESKTOP;
    nokeys(); vtc_tick(); fail_n=0; fail_after=0; keys(3); CK(vtc_tick()==0); CK(g_vtc_active==0); fail_after=-1;
    CK(vtc_dev_tty("tty0")&&vtc_dev_tty("tty6")&&!vtc_dev_tty("tty7")&&!vtc_dev_tty("tty")&&!vtc_dev_tty("tty12")&&!vtc_dev_tty("ttyS0"));
    term_clear(); nokeys(); vtc_cmd("chvt","2"); CK(g_vtc_active==0); CK(vtc_tick()==1); CK(g_vtc_active==2);
    term_clear(); vtc_cmd("tty",""); CK(row_has(0,"/dev/tty2"));
    g_state=ST_DESKTOP; CK(vtc_tick()==1); CK(g_vtc_active==0); CK(g_state==ST_DESKTOP); vtc_cmd("chvt","2"); CK(vtc_tick()==1); CK(g_vtc_active==2); CK(g_state==ST_LIVE); CK(row_has(0,"/dev/tty2"));
    term_clear(); vtc_cmd("chvt","x"); CK(row_has(0,"usage")); vtc_cmd("chvt","8"); CK(vtc_tick()==0);
    g_state=ST_DESKTOP; CK(vtc_tick()==1); CK(g_vtc_active==0); g_uid=0; g_gid=0;
    nokeys(); vtc_tick(); keys(4); CK(vtc_tick()==1); CK(g_vtc_active==5); CK(g_uid==1000&&g_gid==1000); CK(g_vs[5].auth==0); CK(row_has(2,"login:"));
    CK(vtc_login_key('l')==1); CK(vtc_login_key('\n')==1);
    typ("root\n"); CK(row_has(3,"Password:")); typ("\n"); CK(g_vs[5].auth==0&&g_uid==1000); CK(row_has(4,"incorrect"));
    installed=0; term_clear(); typ("anna\n"); CK(row_has(1,"Password:")); typ("bad\n"); CK(g_vs[5].auth==0); term_clear(); typ("lock\n"); CK(row_has(1,"Password:")); typ("x\n"); CK(g_vs[5].auth==0);
    term_clear(); typ("root\n"); CK(!row_has(1,"Password:")); CK(g_vs[5].auth==1&&g_uid==0&&g_gid==0); CK(row_has(1,"$ ")); g_vs[5].auth=0; g_uid=1000; g_gid=1000; installed=1; term_clear();
    typ("lock\nx\n"); CK(g_vs[5].auth==0);
    typ("anna\nbad\n"); CK(g_vs[5].auth==0&&g_uid==1000);
    term_clear(); typ("anna\npw1"); CK(!row_has(1,"pw1")); typ("\n"); CK(g_vs[5].auth==1&&g_uid==1000); CK(g_vs[5].ln==0&&!g_vs[5].lbuf[0]); CK(vtc_login_key('x')==0);
    g_uid=0; g_gid=0; nokeys(); vtc_tick(); keys(5); int sn=sudo_n; CK(vtc_tick()==1); CK(g_vtc_active==6); CK(g_uid==1000); CK(sudo_n>sn);
    sudo_u=777;
    nokeys(); vtc_tick(); keys(4); CK(vtc_tick()==1); CK(g_vtc_active==5); CK(g_uid==0);
    CK(sudo_u==0); sudo_u=555; nokeys(); vtc_tick(); keys(5); CK(vtc_tick()==1); CK(g_vtc_active==6); CK(sudo_u==777); nokeys(); vtc_tick(); keys(4); CK(vtc_tick()==1); CK(sudo_u==555);
    nokeys(); vtc_tick(); keys(6); CK(vtc_tick()==1); CK(g_vtc_active==0); CK(g_uid==0);
    strcpy(g_user,"nobody"); nokeys(); vtc_tick(); keys(3); CK(vtc_tick()==1); CK(g_uid==-1); CK(vtc_login_key('a')==1);
    if(fails){ printf("%d failures\n",fails); return 1; }
    printf("all vtcon tests passed\n"); return 0;
}
