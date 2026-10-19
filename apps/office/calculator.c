#include "include/nvx/base.h"
#include "include/nvx/apps/office/calculator.h"
/* =========================================================================
   Calculator
   ========================================================================= */
i64 calc_acc=0, calc_cur=0; char calc_op=0; int calc_fresh=1, calc_err=0;
int cb_x[20],cb_y[20],cb_w,cb_h; const char *CBL[20]={"C","/","*","<","7","8","9","-","4","5","6","+","1","2","3","=","0","0",".","="};
static void calc_apply(void){ if(calc_op=='+')calc_acc+=calc_cur; else if(calc_op=='-')calc_acc-=calc_cur;
    else if(calc_op=='*')calc_acc*=calc_cur; else if(calc_op=='/'){ if(calc_cur==0)calc_err=1; else calc_acc/=calc_cur; } else calc_acc=calc_cur; }
void calc_press(const char*b){
    if(calc_err){ calc_err=0; calc_acc=0; calc_cur=0; calc_op=0; calc_fresh=1; if(b[0]=='C')return; }
    char k=b[0];
    if(k>='0'&&k<='9'){ if(calc_fresh){calc_cur=0;calc_fresh=0;} calc_cur=calc_cur*10+(k-'0'); }
    else if(k=='C'){ calc_acc=0;calc_cur=0;calc_op=0;calc_fresh=1; }
    else if(k=='<'){ calc_cur/=10; }
    else if(k=='='){ calc_apply(); calc_op=0; calc_cur=calc_acc; calc_fresh=1; }
    else { if(!calc_fresh){calc_apply();} else calc_acc=calc_cur; calc_op=k; calc_fresh=1; }
}
