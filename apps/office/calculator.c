#include "include/nvx/base.h"
#include "include/nvx/apps/office/calculator.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
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
void calc_draw(int i){
    win_frame(i,"Calculator");
    int x=WN[i].x+18,y=WN[i].y+TITLEH+16,w=WN[i].w-36;
    fill_round(x,y,w,52,8,15,rgb(24,28,38));
    char d[24],*p=d; i64 v=calc_fresh&&!calc_op?calc_acc:calc_cur; if(calc_err){scopy(d,"ERROR");}
    else { if(v<0){*p++='-';v=-v;} char t[20];int n=0; if(!v)t[n++]='0'; i64 vv=v; while(vv){t[n++]='0'+(int)(vv%10);vv/=10;} while(n)*p++=t[--n]; *p=0; }
    dstr(x+w-16-slen(d)*16, y+12, d, rgb(240,244,252), 2);
    int gx=x, gy=y+64, gw=(w-3*8)/4, gh=40; cb_w=gw; cb_h=gh;
    for(int k=0;k<16;k++){ int col=k%4, row=k/4; int bx=gx+col*(gw+8), by=gy+row*(gh+8);
        cb_x[k]=bx; cb_y[k]=by; u32 bg=rgb(58,64,80); const char*L=CBL[k];
        if(L[0]=='='||L[0]=='C') bg=ACC; else if(L[0]=='+'||L[0]=='-'||L[0]=='*'||L[0]=='/'||L[0]=='<') bg=rgb(80,90,112);
        fill_round(bx,by,gw,gh,7,15,bg); dstr_ctr(bx+gw/2,by+(gh-16)/2,L,rgb(255,255,255),2); }
}
void calc_click(int px,int py){ for(int k=0;k<16;k++) if(in_rect(px,py,cb_x[k],cb_y[k],cb_w,cb_h)){ calc_press(CBL[k]); return; } }

