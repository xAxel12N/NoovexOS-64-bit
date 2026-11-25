#include <stdio.h>
#include <string.h>
#include "core/sleepsm.c"

static int fails;
#define CK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); fails++; } }while(0)
static char trace[256]; static int tn;
static void tr(char c){ if(tn<255){ trace[tn++]=c; trace[tn]=0; } }
static int a_pres=1, b_pres=1, c_pres=1, b_susfail, c_resfail; static const char* c_why;
static int pa(void){ return a_pres; } static int pb(void){ return b_pres; } static int pc(void){ return c_pres; }
static const char* bc(void){ return c_why; }
static int sa(void){ tr('a'); return 0; } static int ra(void){ tr('A'); return 0; }
static int sb(void){ tr('b'); return b_susfail?-1:0; } static int rb(void){ tr('B'); return 0; }
static int sc(void){ tr('c'); return 0; } static int rc(void){ tr('C'); return c_resfail?-1:0; }
static const struct slp_dev DA={"disk",pa,0,sa,ra}, DB={"Wi-Fi",pb,0,sb,rb}, DC={"USB",pc,bc,sc,rc};
static void reset(void){ tn=0; trace[0]=0; }
int main(void){
    struct slp_sm s; memset(&s,0,sizeof s);
    CK(slp_add(&s,&DA)==0); CK(slp_add(&s,&DB)==1); CK(slp_add(&s,&DC)==2); CK(slp_add(&s,&DA)==0); CK(s.n==3);
    reset(); CK(slp_enter(&s)==0); CK(s.state==SLP_ASLEEP); CK(!strcmp(trace,"abc"));
    CK(slp_enter(&s)==-3);
    reset(); CK(slp_leave(&s)==0); CK(!strcmp(trace,"CBA")); CK(s.state==SLP_IDLE); CK(s.cycles==1);
    CK(slp_leave(&s)==0);
    c_why="a USB stick is being written"; reset(); CK(slp_enter(&s)==-1); CK(s.state==SLP_BLOCKED); CK(!strcmp(trace,""));
    CK(strstr(s.msg,"USB")&&strstr(s.msg,"USB stick is being written")); c_why=0;
    b_susfail=1; reset(); CK(slp_enter(&s)==-2); CK(!strcmp(trace,"abA")); CK(s.state==SLP_FAILED); CK(s.fails==1); CK(strstr(s.msg,"Wi-Fi")!=0); b_susfail=0;
    b_pres=0; reset(); CK(slp_enter(&s)==0); CK(!strcmp(trace,"ac")); reset(); CK(slp_leave(&s)==0); CK(!strcmp(trace,"CA")); b_pres=1;
    c_resfail=1; reset(); CK(slp_enter(&s)==0); reset(); CK(slp_leave(&s)==1); CK(!strcmp(trace,"CBA")); CK(s.state==SLP_FAILED); CK(s.resfail==1); CK(strstr(s.msg,"USB did not come back")!=0); c_resfail=0;
    reset(); CK(slp_enter(&s)==0); CK(slp_leave(&s)==0); CK(s.state==SLP_IDLE);
    c_pres=0; c_why="never asked"; reset(); CK(slp_enter(&s)==0); CK(!strcmp(trace,"ab")); slp_leave(&s); c_pres=1; c_why=0;
    char big[300]; memset(big,'x',299); big[299]=0; c_why=big; CK(slp_enter(&s)==-1); CK(strlen(s.msg)<sizeof s.msg); c_why=0;
    CK(slp_pick(0,1,1,1,1,0)==SLP_MODE_NONE); CK(slp_pick(1,1,1,1,1,0)==SLP_MODE_S3); CK(slp_pick(1,1,0,1,1,0)==SLP_MODE_S2IDLE);
    CK(slp_pick(1,1,0,0,1,0)==SLP_MODE_S1); CK(slp_pick(1,0,0,0,1,1)==SLP_MODE_S2IDLE); CK(slp_pick(1,0,0,0,0,0)==SLP_MODE_S2IDLE);
    CK(!strcmp(slp_state_name(SLP_ASLEEP),"asleep")); CK(!strcmp(slp_state_name(99),"?")); CK(strstr(slp_mode_name(SLP_MODE_S3),"S3")!=0);
    CK(slp_req_due(1000,0,0)==1); CK(slp_req_due(1000,700,1)==1); CK(slp_req_due(1000,900,1)==-1); CK(slp_req_due(1000,900,-1)==-1); CK(slp_req_due(1000,900,0)==0); CK(slp_req_due(1101,900,0)==1);
    struct slp_sm f; memset(&f,0,sizeof f); for(int i=0;i<SLP_MAXDEV;i++){ static struct slp_dev d[SLP_MAXDEV]; d[i].name="d"; CK(slp_add(&f,&d[i])==i); } { static struct slp_dev x={"x",0,0,0,0}; CK(slp_add(&f,&x)==-1); }
    CK(slp_enter(&f)==0); CK(slp_leave(&f)==0);
    if(fails){ printf("SLEEPSM: %d failures\n",fails); return 1; }
    printf("SLEEPSM HOST TEST: ALL PASS\n"); return 0; }
