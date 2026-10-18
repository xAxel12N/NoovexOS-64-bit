#include "include/nvx/base.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/net/http.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/gui/wm.h"
#include "include/nvx/apps/calculator.h"
#include "include/nvx/apps/chatgpt.h"
/* ================= ChatGPT app (talks to the Lovable AI backend over HTTPS) ================= */
#define CG_HOST "id-preview--5b18dc3f-c2ca-42cf-a981-a226787c6564.lovable.app"
#define CG_MAXMSG 24
static struct { int role; char text[400]; } g_cg[CG_MAXMSG];   /* role 0=user 1=assistant 2=system */
static int g_cg_n=0, g_cg_scroll=0;
static char g_cg_in[256]; static int g_cg_inlen=0; static int g_cg_busy=0;
static void cg_push(int role,const char* t){ if(g_cg_n>=CG_MAXMSG){ for(int i=1;i<CG_MAXMSG;i++)g_cg[i-1]=g_cg[i]; g_cg_n=CG_MAXMSG-1; }
    g_cg[g_cg_n].role=role; int i=0; for(;t[i]&&i<399;i++)g_cg[g_cg_n].text[i]=t[i]; g_cg[g_cg_n].text[i]=0; g_cg_n++; }
/* draw the ChatGPT-style knot logo (approximation, ChatGPT green) */
static void cg_logo(int cx,int cy,int r,u32 col){
    fill_round(cx-r,cy-r,r*2,r*2,r-2,15,col);
    u32 w=rgb(255,255,255);
    for(int t=-r+3;t<=r-3;t++){ putpx(cx+t,cy,w); putpx(cx+t,cy+t*6/10,w); putpx(cx+t,cy-t*6/10,w); }
    fill_round(cx-2,cy-2,4,4,2,15,w);
}
static void cg_send(void){
    if(g_cg_busy||g_cg_inlen==0)return; g_cg_busy=1;
    cg_push(0,g_cg_in);
    /* build JSON body with minimal escaping */
    char body[600]; char* p=body; p=scopy(p,"{\"message\":\"");
    for(int i=0;i<g_cg_inlen && p<body+540;i++){ char c=g_cg_in[i]; if(c=='"'||c=='\\'){*p++='\\';*p++=c;} else if(c=='\n'){*p++='\\';*p++='n';} else *p++=c; }
    p=scopy(p,"\",\"source\":\"noovexos\"}"); *p=0; int bl=(int)(p-body);
    g_cg_in[0]=0; g_cg_inlen=0;
    int n=https_request(CG_HOST,"/api/public/chat","POST","Content-Type: application/json\r\n",body,bl,http_buf,(int)sizeof(http_buf));
    if(n<=0){ cg_push(2,"(network error - is NoovexOS online? this needs working HTTPS)"); g_cg_busy=0; return; }
    /* find the body (after the blank line) */
    int he=0; for(int i=0;i+3<n;i++) if(http_buf[i]=='\r'&&http_buf[i+1]=='\n'&&http_buf[i+2]=='\r'&&http_buf[i+3]=='\n'){ he=i+4; break; }
    char reply[400];
    if(json_str(http_buf+he,n-he,"reply",reply,(int)sizeof(reply))>0) cg_push(1,reply);
    else { char err[200]; if(json_str(http_buf+he,n-he,"error",err,(int)sizeof(err))>0){ char m[240]; char* q=m; q=scopy(q,"(server: ");q=scopy(q,err);*q++=')';*q=0; cg_push(2,m); } else cg_push(2,"(no reply in server response)"); }
    g_cg_busy=0;
}
static int g_cg_inx,g_cg_iny,g_cg_inw;   /* input box rect (for clicks) */
void chatgpt_draw(int i){
    win_frame(i,"ChatGPT");
    int wx=WN[i].x, wy=WN[i].y+TITLEH, ww=WN[i].w, wh=WN[i].h-TITLEH;
    fill_rect(wx+1,wy,ww-2,wh-1,rgb(247,247,248));
    /* header */
    fill_rect(wx+1,wy,ww-2,40,rgb(255,255,255)); fill_rect(wx+1,wy+40,ww-2,1,rgb(228,228,232));
    cg_logo(wx+24,wy+20,12,rgb(16,163,127)); dstr(wx+44,wy+13,"ChatGPT",rgb(32,33,40),2);
    if(g_cg_busy) dstr(wx+ww-90,wy+15,"thinking...",rgb(150,120,60),1);
    /* messages */
    int my0=wy+50, myb=wy+wh-52; int y=my0-g_cg_scroll;
    if(g_cg_n==0){ dstr_ctr(wx+ww/2,wy+wh/2-20,"Ask ChatGPT anything.",rgb(150,152,160),1);
        dstr_ctr(wx+ww/2,wy+wh/2+2,"(needs NoovexOS networking / HTTPS)",rgb(180,182,190),1); }
    for(int m=0;m<g_cg_n;m++){ int usr=(g_cg[m].role==0); int sys=(g_cg[m].role==2);
        /* wrap text at ~ (ww-120)/8 chars */
        int cols=(ww-120)/8; if(cols<10)cols=10; const char* t=g_cg[m].text; int len=0; while(t[len])len++;
        int lines=(len+cols-1)/cols; if(lines<1)lines=1; int bh=lines*16+14; int bw=ww-90;
        int bx=usr?(wx+ww-bw-16):(wx+16);
        u32 bub=sys?rgb(250,235,235):usr?rgb(24,119,242):rgb(236,236,238);
        u32 tc =sys?rgb(160,60,60):usr?rgb(255,255,255):rgb(40,42,50);
        if(y+bh>my0-20 && y<myb){ fill_round(bx,y,bw,bh,10,15,bub);
            for(int ln=0;ln<lines;ln++){ char line[130]; int c=0; for(;c<cols && t[ln*cols+c];c++)line[c]=t[ln*cols+c]; line[c]=0; dstr(bx+12,y+8+ln*16,line,tc,1); } }
        y+=bh+8;
    }
    int total=y+g_cg_scroll-my0; int viewh=myb-my0; if(total>viewh) g_cg_scroll=g_cg_scroll; /* keep */
    /* auto-scroll to bottom when content overflows */
    if(total>viewh && g_cg_scroll<total-viewh) g_cg_scroll=total-viewh;
    if(g_cg_scroll<0)g_cg_scroll=0;
    /* input box */
    g_cg_inx=wx+14; g_cg_iny=wy+wh-44; g_cg_inw=ww-100;
    fill_round(g_cg_inx,g_cg_iny,g_cg_inw,34,8,15,rgb(255,255,255)); 
    { int over=(mx>=g_cg_inx&&mx<g_cg_inx+g_cg_inw&&my>=g_cg_iny&&my<g_cg_iny+34)||focus==i; if(over){ for(int t=0;t<g_cg_inw;t++){putpx(g_cg_inx+t,g_cg_iny,rgb(16,163,127));putpx(g_cg_inx+t,g_cg_iny+33,rgb(16,163,127));} } }
    if(g_cg_inlen==0) dstr(g_cg_inx+12,g_cg_iny+10,"Message ChatGPT...",rgb(170,172,180),1);
    else { char v[200]; int c=0,st=g_cg_inlen>((g_cg_inw-24)/8)?g_cg_inlen-((g_cg_inw-24)/8):0; for(;st+c<g_cg_inlen&&c<190;c++)v[c]=g_cg_in[st+c]; v[c]=0; dstr(g_cg_inx+12,g_cg_iny+10,v,rgb(32,33,40),1);
        if(((g_ticks/30)&1)) fill_rect(g_cg_inx+12+c*8,g_cg_iny+8,2,16,rgb(32,33,40)); }
    /* send button */
    int sbx=wx+ww-78, sby=wy+wh-44; fill_round(sbx,sby,64,34,8,15, g_cg_busy?rgb(180,200,190):rgb(16,163,127)); dstr(sbx+14,sby+10,"Send",rgb(255,255,255),1);
}
void chatgpt_key(char c){
    if(c=='\r'||c=='\n'){ cg_send(); return; }
    if(c==8){ if(g_cg_inlen>0)g_cg_in[--g_cg_inlen]=0; return; }
    if(c>=32 && c<127 && g_cg_inlen<255){ g_cg_in[g_cg_inlen++]=c; g_cg_in[g_cg_inlen]=0; }
}
void chatgpt_click(int px,int py){
    int wx=WN[W_CHATGPT].x, wy=WN[W_CHATGPT].y+TITLEH, ww=WN[W_CHATGPT].w, wh=WN[W_CHATGPT].h-TITLEH;
    int sbx=wx+ww-78, sby=wy+wh-44; if(in_rect(px,py,sbx,sby,64,34)){ cg_send(); return; }
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

