#include "include/nvx/base.h"
#include "include/nvx/apps/office/calculator.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
/* =========================================================================
   Calculator
   ========================================================================= */
i64 calc_acc=0, calc_cur=0; char calc_op=0; int calc_fresh=1, calc_err=0, calc_dec=-1;
int cb_x[20],cb_y[20],cb_w,cb_h; const char *CBL[20]={"C","/","*","<","7","8","9","-","4","5","6","+","1","2","3","%","+/-","0",".","="};
#define CSC 1000000LL
#define CDIG 6
static const i64 CPOW[CDIG+1]={1,10,100,1000,10000,100000,1000000};
static u64 cu_div128(unsigned __int128 n,u64 d){ unsigned __int128 q=0,r=0; for(int b=127;b>=0;b--){ r=(r<<1)|((n>>b)&1); q<<=1; if(r>=d){ r-=d; q|=1; } } if(r*2>=d)q++; if(q>>63){ calc_err=1; return 0; } return (u64)q; }
static i64 c_muldiv(i64 a,i64 b,i64 d){ int neg=(a<0)^(b<0)^(d<0); u64 ua=a<0?(u64)0-(u64)a:(u64)a, ub=b<0?(u64)0-(u64)b:(u64)b, ud=d<0?(u64)0-(u64)d:(u64)d;
    u64 q=cu_div128((unsigned __int128)ua*ub,ud); return neg?-(i64)q:(i64)q; }
static void calc_apply(void){ i64 r=calc_cur;
    if(calc_op=='+'){ if(__builtin_add_overflow(calc_acc,calc_cur,&r))calc_err=1; }
    else if(calc_op=='-'){ if(__builtin_sub_overflow(calc_acc,calc_cur,&r))calc_err=1; }
    else if(calc_op=='*') r=c_muldiv(calc_acc,calc_cur,CSC);
    else if(calc_op=='/'){ if(calc_cur==0){ calc_err=1; r=0; } else r=c_muldiv(calc_acc,CSC,calc_cur); }
    calc_acc=calc_err?0:r; }
void calc_press(const char*b){
    if(calc_err){ calc_err=0; calc_acc=0; calc_cur=0; calc_op=0; calc_fresh=1; calc_dec=-1; if(b[0]=='C')return; }
    char k=b[0];
    if(k>='0'&&k<='9'){ if(calc_fresh||calc_dec==-2){calc_cur=0;calc_fresh=0;calc_dec=-1;} i64 d=k-'0', m=calc_cur<0?-calc_cur:calc_cur;
        if(calc_dec<0){ if(m<=(0x7fffffffffffffffLL-d*CSC)/10) m=m*10+d*CSC; }
        else if(calc_dec<CDIG){ m+=d*CPOW[CDIG-1-calc_dec]; calc_dec++; }
        calc_cur=calc_cur<0?-m:m; }
    else if(k=='.'){ if(calc_fresh||calc_dec==-2){calc_cur=0;calc_fresh=0;calc_dec=-1;} if(calc_dec<0)calc_dec=0; }
    else if(k=='C'){ calc_acc=0;calc_cur=0;calc_op=0;calc_fresh=1;calc_dec=-1; }
    else if(k=='<'){ if(calc_fresh||calc_dec==-2)return; i64 m=calc_cur<0?-calc_cur:calc_cur;
        if(calc_dec>0){ calc_dec--; m-=m%CPOW[CDIG-calc_dec]; } else if(calc_dec==0) calc_dec=-1; else m=m/CSC/10*CSC;
        calc_cur=calc_cur<0?-m:m; }
    else if(k=='+'&&b[1]=='/'){ if(calc_fresh&&!calc_op){ calc_acc=-calc_acc; calc_cur=calc_acc; } else { calc_cur=-calc_cur; if(calc_fresh){ calc_fresh=0; calc_dec=-2; } } }
    else if(k=='%'){ if(calc_fresh&&!calc_op)calc_cur=calc_acc; calc_cur=(calc_op=='+'||calc_op=='-')?c_muldiv(calc_acc,calc_cur,100*CSC):c_muldiv(calc_cur,1,100);
        if(calc_op){ calc_fresh=0; calc_dec=-2; } else { calc_acc=calc_cur; calc_fresh=1; calc_dec=-1; } }
    else if(k=='='){ calc_apply(); calc_op=0; calc_cur=calc_acc; calc_fresh=1; calc_dec=-1; }
    else { if(!calc_fresh||!calc_op){ if(calc_op)calc_apply(); else calc_acc=calc_cur; } calc_op=k; calc_fresh=1; calc_dec=-1; }
}
void calc_key(int c){
    char b[4]={0,0,0,0};
    if((c>='0'&&c<='9')||c=='+'||c=='-'||c=='*'||c=='/'||c=='%'||c=='.') b[0]=(char)c;
    else if(c==',') b[0]='.';
    else if(c=='='||c=='\n'||c=='\r') b[0]='=';
    else if(c=='\b'||c==127) b[0]='<';
    else if(c==27||c=='c'||c=='C') b[0]='C';
    else return;
    calc_press(b);
}
static void calc_fmt(char*d,i64 v,int dec){
    char*p=d; u64 m=v<0?(u64)0-(u64)v:(u64)v; if(v<0)*p++='-';
    u64 ip=m/(u64)CSC, fp=m%(u64)CSC; char t[24]; int n=0; if(!ip)t[n++]='0'; while(ip){ t[n++]=(char)('0'+ip%10); ip/=10; } while(n)*p++=t[--n];
    int nd=dec; if(nd<0){ nd=CDIG; while(nd>0&&fp%(u64)CPOW[CDIG-nd+1]==0)nd--; }
    if(dec>=0||nd>0){ *p++='.'; for(int k=0;k<nd;k++){ *p++=(char)('0'+(fp/(u64)CPOW[CDIG-1-k])%10); } }
    *p=0; }
void calc_draw(int i){
    win_frame(i,"Calculator");
    int x=WN[i].x+18,y=WN[i].y+TITLEH+16,w=WN[i].w-36;
    fill_round(x,y,w,52,8,15,rgb(24,28,38));
    char d[40]; if(calc_err)scopy(d,"ERROR"); else if(calc_fresh&&!calc_op) calc_fmt(d,calc_acc,-1); else calc_fmt(d,calc_cur,calc_fresh?-1:calc_dec);
    { int sc=(slen(d)*16<=w-24)?2:1; dstr(x+w-16-slen(d)*8*sc, y+(sc==2?12:20), d, rgb(240,244,252), sc); }
    int gx=x, gy=y+64, gw=(w-3*8)/4, gh=40; cb_w=gw; cb_h=gh;
    for(int k=0;k<20;k++){ int col=k%4, row=k/4; int bx=gx+col*(gw+8), by=gy+row*(gh+8);
        cb_x[k]=bx; cb_y[k]=by; u32 bg=rgb(58,64,80); const char*L=CBL[k];
        if(L[0]=='='||L[0]=='C') bg=ACC; else if(L[1]=='/') bg=rgb(58,64,80); else if(L[0]=='%'||L[0]=='+'||L[0]=='-'||L[0]=='*'||L[0]=='/'||L[0]=='<') bg=rgb(80,90,112);
        fill_round(bx,by,gw,gh,7,15,bg); dstr_ctr(bx+gw/2,by+(gh-16)/2,L,rgb(255,255,255),2); }
}
void calc_click(int px,int py){ for(int k=0;k<20;k++) if(in_rect(px,py,cb_x[k],cb_y[k],cb_w,cb_h)){ calc_press(CBL[k]); return; } }

