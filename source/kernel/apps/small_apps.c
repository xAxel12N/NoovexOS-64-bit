#include "include/nvx/base.h"
#include "include/nvx/mm/heap_api.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/arch/x86_64/cpuinfo.h"
#include "include/nvx/drivers/rtc/cmos.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/net/nic_names.h"
#include "include/nvx/drivers/net/nic.h"
#include "include/nvx/net/tcpip.h"
#include "include/nvx/net/http.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/gui/wm.h"
#include "include/nvx/apps/files_classic.h"
#include "include/nvx/apps/small_apps.h"
#include "include/nvx/gui/desktop.h"
#include "include/nvx/init/setup.h"
/* ---- Clock ---- */
static const short SINT[91]={0,17,35,52,70,87,105,122,139,156,174,191,208,225,242,259,276,292,309,326,342,358,375,391,407,423,438,454,469,485,500,515,530,545,559,574,588,602,616,629,643,656,669,682,695,707,719,731,743,755,766,777,788,799,809,819,829,839,848,857,866,875,883,891,899,906,914,921,927,934,940,946,951,956,961,966,970,974,978,982,985,988,990,993,995,996,998,999,999,1000,1000};
int isin(int deg){ deg%=360; if(deg<0)deg+=360; if(deg<=90)return SINT[deg]; if(deg<=180)return SINT[180-deg]; if(deg<=270)return -SINT[deg-180]; return -SINT[360-deg]; }
int icos(int deg){ return isin(deg+90); }
static void clock_hand(int cx,int cy,int ang,int len,int wdt,u32 col){
    int ex=cx+icos(ang-90)*len/1000, ey=cy+isin(ang-90)*len/1000;
    for(int w=-wdt;w<=wdt;w++){ line_bl(cx+w,cy,ex+w,ey,col,255,1); line_bl(cx,cy+w,ex,ey+w,col,255,1); }
}
void clock_draw(int i){
    win_frame(i,"Clock");
    int h,m,s,yr,mo,dy; read_clock(&h,&m,&s,&yr,&mo,&dy);
    int cx=WN[i].x+WN[i].w/2, cy=WN[i].y+TITLEH+30+ (WN[i].w-60)/2, R=(WN[i].w-90)/2; if(R>180)R=180;
    fill_circle(cx,cy,R+10,rgb(240,242,248)); fill_circle(cx,cy,R+6,ACC); fill_circle(cx,cy,R+2,rgb(252,253,255));
    for(int t=0;t<60;t++){ int a=t*6; int o1=(t%5==0)?R-14:R-7; int x1=cx+icos(a-90)*o1/1000,y1=cy+isin(a-90)*o1/1000;
        int x2=cx+icos(a-90)*R/1000,y2=cy+isin(a-90)*R/1000; u32 c=(t%5==0)?rgb(60,66,86):rgb(180,186,200); line_bl(x1,y1,x2,y2,c,255,(t%5==0)?2:1); }
    int ha=(h%12)*30 + m/2;
    clock_hand(cx,cy,ha,R*52/100,2,rgb(40,46,64));
    clock_hand(cx,cy,m*6,R*74/100,2,rgb(40,46,64));
    clock_hand(cx,cy,s*6,R*82/100,0,rgb(220,70,90));
    fill_circle(cx,cy,5,rgb(40,46,64)); fill_circle(cx,cy,2,rgb(220,70,90));
    char t[16],*p=t; p=u2(p,h);*p++=':';p=u2(p,m);*p++=':';p=u2(p,s);*p=0;
    dstr_ctr(cx,cy+R+22,t,INK,3);
    static const char*MON[13]={"","Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    char d[32],*q=d; q=udec(q,(u32)dy);*q++=' '; { const char*mn=MON[(mo>=1&&mo<=12)?mo:0]; while(*mn)*q++=*mn++; } *q++=' '; q=udec(q,(u32)yr); *q=0;
    dstr_ctr(cx,cy+R+56,d,rgb(90,100,120),1);
}
/* ---- Task Manager ---- */
static const char* pci_class_name(u8 cl,u8 sub){
    switch(cl){ case 0x00:return "Legacy device"; case 0x01: return sub==0x06?"SATA controller":sub==0x08?"NVMe controller":sub==0x01?"IDE controller":"Storage controller";
        case 0x02:return "Network controller"; case 0x03:return "Display controller"; case 0x04:return "Multimedia device"; case 0x05:return "Memory controller";
        case 0x06:return sub==0x00?"Host bridge":sub==0x01?"ISA bridge":sub==0x04?"PCI bridge":"Bridge"; case 0x07:return "Comm controller"; case 0x08:return "System peripheral";
        case 0x09:return "Input device"; case 0x0C:return sub==0x03?"USB controller":"Serial bus"; case 0x0D:return "Wireless controller"; default:return "PCI device"; }
}
   /* per-app glyph, defined later */
/* ---- Task Manager: Windows-11-style process table ---- */
static int tm_bx[NWIN],tm_by[NWIN],tm_bi[NWIN],tm_bn=0;   /* per-row hit -> window index */
static int g_tmsel=-1;                                    /* selected app row (window index) */
static int tm_endx,tm_endy,tm_endw;                       /* End task button rect */
/* representative per-app memory (KB) - windows are the "apps"; heavy ones cost more */
static int tm_app_kb(int a){ switch(a){
    case W_BROWSER:return 149504; case W_DOOM:return 33792; case W_PAINT:return 18432;
    case W_FILES:return 9216; case W_SETTINGS:return 7168; case W_EDITOR:return 5120;
    case W_NEOFETCH:return 2560; case W_TERM:return 2048; case W_CALENDAR:return 4096;
    case W_CLOCK:return 1536; case W_CALC:return 1280; case W_SNAKE:return 1536; case W_FLAPPY:return 1792;
    default:return 3072; } }
/* background subsystems that are genuinely running (not fabricated processes) */
static const char* TM_BG_N[7]={"System","Compositor","Input (i8042)","Network stack","NVXFS","TLS engine","Heap manager"};
static const int   TM_BG_KB[7]={20480,15360,1024,6144,4096,3072,2048};
static u32 heat(int val,int mx,int dark){ if(val<0)val=0; if(val>mx)val=mx; int t=mx?val*255/mx:0;
    u32 base=dark?rgb(32,34,44):rgb(246,247,250); u32 hot=dark?rgb(150,64,38):rgb(250,204,168); return mix(base,hot,t); }
static char* kb_str(char*p,int kb){ if(kb>=1024){ int mb10=kb*10/1024; p=udec(p,mb10/10);*p++='.';*p++=(char)('0'+mb10%10);p=scopy(p," MB"); } else { p=udec(p,kb);p=scopy(p," KB"); } return p; }
static void tm_row(int rx,int ry,int rw,int rh,int cpu,int kb,int diskkb,int net,int cpu_mx,int mem_mx,int dark,int sel){
    u32 tx=dark?rgb(232,236,244):rgb(28,32,44);
    int cCPU=rx+rw-312, cMEM=rx+rw-234, cDISK=rx+rw-150, cNET=rx+rw-66;   /* cell left edges (78 wide) */
    fill_rect(cCPU,ry,76,rh,sel?ACC:heat(cpu,cpu_mx,dark));
    fill_rect(cMEM,ry,76,rh,sel?ACC:heat(kb,mem_mx,dark));
    fill_rect(cDISK,ry,76,rh,sel?ACC:heat(diskkb,4096,dark));
    fill_rect(cNET,ry,76,rh,sel?ACC:heat(net,1000,dark));
    u32 vt=sel?rgb(255,255,255):tx; char b[24],*p;
    p=b;{int c10=cpu; p=udec(p,c10/10);*p++='.';*p++=(char)('0'+c10%10);*p++='%';*p=0;} dstr(cCPU+72-slen(b)*8,ry+rh/2-4,b,vt,1);
    p=b;p=kb_str(p,kb);*p=0; dstr(cMEM+72-slen(b)*8,ry+rh/2-4,b,vt,1);
    p=b;if(diskkb){int d10=diskkb*10/1024;p=udec(p,d10/10);*p++='.';*p++=(char)('0'+d10%10);p=scopy(p," MB/s");}else p=scopy(p,"0 MB/s");*p=0; dstr(cDISK+72-slen(b)*8,ry+rh/2-4,b,vt,1);
    p=b;if(net){p=udec(p,net);p=scopy(p," Kb/s");}else p=scopy(p,"0 Mbit/s");*p=0; dstr(cNET+72-slen(b)*8,ry+rh/2-4,b,vt,1);
}
static void sysmon_draw_old(int i){
    win_frame(i,"Task Manager");
    int dark=g_theme; int wx=WN[i].x, wy=WN[i].y+TITLEH, ww=WN[i].w, wh=WN[i].h-TITLEH;
    u32 bg=dark?rgb(24,26,34):rgb(255,255,255), panel=dark?rgb(30,33,43):rgb(247,248,251);
    u32 ink=dark?rgb(236,239,246):rgb(24,28,40), sub=dark?rgb(150,158,176):rgb(110,118,138);
    fill_rect(wx,wy,ww,wh,bg);
    /* left rail (Windows 11) */
    int rail=48; fill_rect(wx,wy,rail,wh,panel);
    const char* rlbl[6]={"P","G","H","U","D","S"};   /* Processes, Performance, App history, Users, Details, Services */
    for(int r=0;r<6;r++){ int ry=wy+16+r*46; if(r==0){ fill_round(wx+6,ry-6,rail-12,38,8,15,dark?rgb(44,48,62):rgb(230,234,244)); fill_rect(wx,ry-6,3,38,ACC); }
        dstr_ctr(wx+rail/2,ry+4,rlbl[r],r==0?ACC:sub,2); }
    int cx=wx+rail+18, cy=wy+14, cr=wx+ww;
    dstr(cx,cy,"Processes",ink,2); cy+=40;
    /* count apps + totals */
    int napp=0; long memkb=0, tcpu=0;
    for(int a=0;a<NWIN;a++) if(WN[a].open){ napp++; memkb+=tm_app_kb(a); if(a==focus)tcpu+=18; else tcpu+=2; }
    for(int b=0;b<7;b++) memkb+=TM_BG_KB[b];
    long total_kb=(long)(g_ram_mb?g_ram_mb:2048)*1024L; long usedkb=memkb + (heap_used()/1024) + 24576;
    int mempct = total_kb? (int)(usedkb*1000/total_kb):0;   /* tenths of % */
    int cpupct = (int)tcpu; if(cpupct>999)cpupct=999;
    /* header row with per-column totals (Windows shows the total above each column) */
    int hx=cx, hy=cy, rh=26;
    dstr(hx,hy,"Name",sub,1);
    struct { const char* lbl; int val; int unit; } col[4]={{"CPU",cpupct,0},{"Memory",mempct,0},{"Disk",3,1},{"Network",0,2}};
    int cxs[4]={cr-312-rail+rail, cr-234, cr-150, cr-66}; (void)cxs;
    int cedge[4]={cr-312,cr-234,cr-150,cr-66};
    for(int k=0;k<4;k++){ char b[20],*p=b; if(k<2){p=udec(p,col[k].val/10);*p++='%';*p=0;}else if(k==2){p=scopy(p,"1%");}else{p=scopy(p,"0%");}
        u32 hc=(k<2 && col[k].val/10>=50)?rgb(224,96,72):sub;
        dstr(cedge[k]+72-slen(b)*8,hy-2,b,hc,1); dstr(cedge[k]+72-slen(col[k].lbl)*8,hy+12,(char*)col[k].lbl,sub,1); }
    cy+=rh+6; fill_rect(cx,cy-4,cr-cx-14,1,dark?rgb(50,54,68):rgb(228,231,238));
    int maxy=wy+wh-52; tm_bn=0;
    /* ---- Apps section ---- */
    { char h[24],*p=h;p=scopy(p,"Apps (");p=udec(p,(u32)napp);*p++=')';*p=0; dstr(cx,cy,h,sub,1); } cy+=24;
    for(int a=0;a<NWIN && cy<maxy;a++){ if(!WN[a].open)continue;
        int sel=(a==g_tmsel); int rowh=30;
        if(sel) fill_rect(cx-4,cy-4,cr-cx-10,rowh,dark?rgb(40,60,96):rgb(225,236,252));
        launch_icon(a==W_DOOM?W_SYSMON:a, cx-4, cy-6);   /* small app glyph */
        dstr(cx+40,cy+3,APPNAME[a],sel?rgb(255,255,255):ink,1);
        if(WN[a].min) dstr(cx+40+slen(APPNAME[a])*8+10,cy+3,"(suspended)",sub,1);
        tm_row(cx,cy-4,cr-cx-10,rowh,a==focus?180:5,tm_app_kb(a),a==focus?307:0,0,300,200000,dark,sel);
        tm_bx[tm_bn]=cx-4; tm_by[tm_bn]=cy-4; tm_bi[tm_bn]=a; tm_bn++;
        cy+=rowh;
    }
    cy+=6;
    /* ---- Background processes ---- */
    { char h[36],*p=h;p=scopy(p,"Background processes (");p=udec(p,7);*p++=')';*p=0; dstr(cx,cy,h,sub,1); } cy+=24;
    for(int b=0;b<7 && cy<maxy;b++){ int rowh=28;
        fill_round(cx-2,cy,16,16,3,15,dark?rgb(70,76,96):rgb(150,160,182));
        dstr(cx+40,cy+2,(char*)TM_BG_N[b],ink,1);
        tm_row(cx,cy-3,cr-cx-10,rowh,b==0?40:2,TM_BG_KB[b],0,b==3?12:0,300,200000,dark,0);
        cy+=rowh;
    }
    /* toolbar: End task (bottom, acts on selected app) */
    int by=wy+wh-40; fill_rect(wx+rail,by-8,ww-rail,1,dark?rgb(50,54,68):rgb(228,231,238));
    tm_endw=118; tm_endx=cr-tm_endw-14; tm_endy=by-2;
    int en=(g_tmsel>=0 && g_tmsel<NWIN && WN[g_tmsel].open);
    fill_round(tm_endx,tm_endy,tm_endw,28,7,15,en?ACC:(dark?rgb(44,48,62):rgb(232,234,240)));
    dstr_ctr(tm_endx+tm_endw/2,tm_endy+7,"End task",en?rgb(255,255,255):sub,1);
}
static void sysmon_click_old(int px,int py){
    if(in_rect(px,py,tm_endx,tm_endy,tm_endw,28)){ if(g_tmsel>=0&&g_tmsel<NWIN&&WN[g_tmsel].open){ close_win(g_tmsel); g_tmsel=-1; } return; }
    for(int k=0;k<tm_bn;k++) if(in_rect(px,py,tm_bx[k],tm_by[k],WN[W_SYSMON].w-80,30)){ g_tmsel=tm_bi[k]; return; }
}
/* ---- Calendar ---- */
int g_calyr=0, g_calmo=0, cal_lx,cal_ly,cal_rx,cal_ry,cal_aw2;
int cal_dow(int y,int m,int d){ static const int t[12]={0,3,2,5,0,3,5,1,4,6,2,4}; if(m<3)y-=1; return (y + y/4 - y/100 + y/400 + t[m-1] + d)%7; }
int cal_dim(int y,int m){ static const int d[13]={0,31,28,31,30,31,30,31,31,30,31,30,31}; if(m==2 && ((y%4==0&&y%100!=0)||y%400==0)) return 29; return d[m]; }
void cal_open(void){ int h,m,s,y,mo,dy; read_clock(&h,&m,&s,&y,&mo,&dy); g_calyr=y; g_calmo=mo; }
void cal_key(int c){ if(g_calyr==0)cal_open(); if(c==17){ if(--g_calmo<1){g_calmo=12;g_calyr--;} } else if(c==18){ if(++g_calmo>12){g_calmo=1;g_calyr++;} } }
void calendar_draw(int i){
    win_frame(i,"Calendar");
    if(g_calyr==0)cal_open();
    int hh,mm,ss,cy_,cmo,cdy; read_clock(&hh,&mm,&ss,&cy_,&cmo,&cdy);
    int x0=WN[i].x+22, y0=WN[i].y+TITLEH+18, ww=WN[i].w-44;
    static const char*MON[13]={"","January","February","March","April","May","June","July","August","September","October","November","December"};
    char hd[32],*p=hd; { const char*mn=MON[g_calmo>=1&&g_calmo<=12?g_calmo:1]; while(*mn)*p++=*mn++; } *p++=' '; p=udec(p,(u32)g_calyr); *p=0;
    dstr_ctr(WN[i].x+WN[i].w/2, y0, hd, INK, 2);
    cal_lx=x0-4; cal_ly=y0-8; cal_aw2=30; dstr(x0, y0-2, "<", ACC, 2);
    cal_rx=WN[i].x+WN[i].w-40; cal_ry=y0-8; dstr(cal_rx+4, y0-2, ">", ACC, 2);
    int gy=y0+42, colw=ww/7;
    static const char*WD[7]={"Mo","Tu","We","Th","Fr","Sa","Su"};
    for(int c=0;c<7;c++) dstr_ctr(x0+colw*c+colw/2, gy, WD[c], rgb(140,148,166),1);
    gy+=26;
    int col0=(cal_dow(g_calyr,g_calmo,1)+6)%7, dim=cal_dim(g_calyr,g_calmo);
    int rowh=(WN[i].h-(gy-WN[i].y)-16)/6; if(rowh>46)rowh=46;
    for(int day=1, cell=col0; day<=dim; day++, cell++){ int r=cell/7, c=cell%7; int cxp=x0+colw*c+colw/2, cyp=gy+r*rowh+rowh/2;
        int today=(g_calyr==cy_&&g_calmo==cmo&&day==cdy);
        if(today) fill_circle(cxp, cyp-6, 15, ACC);
        char db[4],*q=db; q=udec(q,(u32)day); *q=0; dstr_ctr(cxp, cyp-12, db, today?rgb(255,255,255):rgb(52,60,78), 1); }
}
/* ---- Snake ---- */
#define SN_GW 21
#define SN_GH 15
static u8 snx[SN_GW*SN_GH], sny[SN_GW*SN_GH]; int snlen, sdx, sdy, applex, appley, snake_score, snake_state; u64 snake_last; static u32 sn_rng=2463534242u;
static u32 snr(void){ sn_rng^=sn_rng<<13; sn_rng^=sn_rng>>17; sn_rng^=sn_rng<<5; return sn_rng; }
static void snake_place_apple(void){ for(int tr=0;tr<300;tr++){ int ax=snr()%SN_GW, ay=snr()%SN_GH; int hit=0; for(int k=0;k<snlen;k++)if(snx[k]==ax&&sny[k]==ay){hit=1;break;} if(!hit){applex=ax;appley=ay;return;} } }
void snake_reset(void){ snlen=4; for(int k=0;k<snlen;k++){ snx[k]=(u8)(8-k); sny[k]=SN_GH/2; } sdx=1; sdy=0; snake_score=0; snake_state=1; sn_rng^=(u32)g_ticks*2654435761u; snake_place_apple(); snake_last=g_ticks; }
void snake_step(void){ int nx=snx[0]+sdx, ny=sny[0]+sdy;
    if(nx<0||nx>=SN_GW||ny<0||ny>=SN_GH){ snake_state=2; return; }
    for(int k=0;k<snlen;k++) if(snx[k]==nx&&sny[k]==ny){ snake_state=2; return; }
    int grow=(nx==applex&&ny==appley); if(grow&&snlen<SN_GW*SN_GH)snlen++;
    for(int k=snlen-1;k>0;k--){ snx[k]=snx[k-1]; sny[k]=sny[k-1]; }
    snx[0]=(u8)nx; sny[0]=(u8)ny;
    if(grow){ snake_score++; snake_place_apple(); } }
void snake_key(int c){ if(snake_state==2){ snake_reset(); return; }
    if(c==17&&sdx!=1){sdx=-1;sdy=0;} else if(c==18&&sdx!=-1){sdx=1;sdy=0;}
    else if(c==19&&sdy!=1){sdx=0;sdy=-1;} else if(c==20&&sdy!=-1){sdx=0;sdy=1;} }
void snake_draw(int i){
    win_frame(i,"Snake");
    char b[24],*p=b; p=scopy(p,"Score ");p=udec(p,(u32)snake_score);*p=0; dstr(WN[i].x+20,WN[i].y+TITLEH+12,b,INK,1);
    int cell=(WN[i].w-40)/SN_GW, bw=cell*SN_GW, bh=cell*SN_GH; int bx=WN[i].x+20, by=WN[i].y+TITLEH+40;
    fill_round(bx,by,bw,bh,8,15,rgb(20,26,46));
    fill_round(bx+applex*cell+2, by+appley*cell+2, cell-4, cell-4, 3,15, rgb(228,76,74));
    for(int k=0;k<snlen;k++){ u32 col=(k==0)?rgb(120,236,152):rgb(57,196,106); fill_round(bx+snx[k]*cell+1, by+sny[k]*cell+1, cell-2, cell-2, 3,15, col); }
    if(snake_state==2){ dstr_ctr(WN[i].x+WN[i].w/2, by+bh/2-12, "Game over", rgb(255,255,255),3); dstr_ctr(WN[i].x+WN[i].w/2, by+bh/2+22, "press an arrow key to restart", rgb(206,212,226),1); }
    else dstr_ctr(WN[i].x+WN[i].w/2, by+bh+10, "Arrow keys to steer", rgb(120,128,146),1);
}
/* ---- Flappy Bird ---- */
int fb_y, fb_vy, fb_state, fb_score, fb_best=0; u64 flappy_last; static u32 fb_rng=88172645u;
#define FB_NP 3
#define FB_PW 46
#define FB_GAP 128
#define FB_SP 190
static int fb_px[FB_NP], fb_gy[FB_NP], fb_scored[FB_NP];
static u32 fbr(void){ fb_rng^=fb_rng<<13; fb_rng^=fb_rng>>17; fb_rng^=fb_rng<<5; return fb_rng; }
static void fb_dims(int*bw,int*bh){ *bw=WN[W_FLAPPY].w-24; *bh=WN[W_FLAPPY].h-TITLEH-64; }
void flappy_reset(void){ int bw,bh; fb_dims(&bw,&bh); fb_y=bh/2; fb_vy=0; fb_score=0; fb_state=1; fb_rng^=(u32)g_ticks*2654435761u;
    int span=bh-FB_GAP-60; if(span<40)span=40;
    for(int k=0;k<FB_NP;k++){ fb_px[k]=bw+30+k*FB_SP; fb_gy[k]=30+(int)(fbr()%(u32)span); fb_scored[k]=0; } flappy_last=g_ticks; }
void flappy_step(void){ int bw,bh; fb_dims(&bw,&bh); int bx=bw/3, bsz=22; int span=bh-FB_GAP-60; if(span<40)span=40;
    fb_vy+=1; if(fb_vy>11)fb_vy=11; fb_y+=fb_vy;
    for(int k=0;k<FB_NP;k++){ fb_px[k]-=3;
        if(fb_px[k] < -FB_PW){ int mxr=0; for(int j=0;j<FB_NP;j++) if(fb_px[j]>mxr)mxr=fb_px[j]; fb_px[k]=mxr+FB_SP; fb_gy[k]=30+(int)(fbr()%(u32)span); fb_scored[k]=0; }
        if(!fb_scored[k] && fb_px[k]+FB_PW < bx){ fb_scored[k]=1; fb_score++; if(fb_score>fb_best)fb_best=fb_score; }
        if(bx+bsz>fb_px[k] && bx<fb_px[k]+FB_PW){ if(fb_y<fb_gy[k] || fb_y+bsz>fb_gy[k]+FB_GAP) fb_state=2; } }
    if(fb_y<0){fb_y=0;fb_vy=0;} if(fb_y+bsz>=bh){ fb_y=bh-bsz; fb_state=2; } }
void flappy_flap(void){ if(fb_state==2){ flappy_reset(); return; } fb_vy=-8; }
void flappy_key(int c){ if(c==' '||c==19||c=='\n') flappy_flap(); }
void flappy_draw(int i){
    win_frame(i,"Flappy Bird");
    char b[24],*p=b; p=scopy(p,"Score ");p=udec(p,(u32)fb_score);*p=0; dstr(WN[i].x+20,WN[i].y+TITLEH+12,b,INK,1);
    p=b; p=scopy(p,"Best ");p=udec(p,(u32)fb_best);*p=0; dstr(WN[i].x+WN[i].w-slen(b)*8-20,WN[i].y+TITLEH+12,b,rgb(120,128,146),1);
    int bw,bh; fb_dims(&bw,&bh); int ox=WN[i].x+12, oy=WN[i].y+TITLEH+40;
    fill_round(ox,oy,bw,bh,8,15,rgb(120,196,240));
    u32 pg=rgb(76,180,96), pgd=rgb(52,138,72);
    for(int k=0;k<FB_NP;k++){ int px=ox+fb_px[k]; if(px>ox+bw||px+FB_PW<ox)continue; int gy=fb_gy[k];
        if(px<ox){ int cut=ox-px; fill_rect(ox,oy,FB_PW-cut,gy,pg); fill_rect(ox,oy+gy+FB_GAP,FB_PW-cut,bh-(gy+FB_GAP),pg); }
        else if(px+FB_PW>ox+bw){ int wv=ox+bw-px; fill_rect(px,oy,wv,gy,pg); fill_rect(px,oy+gy+FB_GAP,wv,bh-(gy+FB_GAP),pg); }
        else { fill_rect(px,oy,FB_PW,gy,pg); fill_rect(px,oy+gy-12,FB_PW,12,pgd); fill_rect(px,oy+gy+FB_GAP,FB_PW,bh-(gy+FB_GAP),pg); fill_rect(px,oy+gy+FB_GAP,FB_PW,12,pgd); } }
    fill_rect(ox,oy+bh-12,bw,12,rgb(222,196,120));
    int bx=ox+bw/3, byd=oy+fb_y, bsz=22, ccx=bx+bsz/2, ccy=byd+bsz/2;
    fill_circle(ccx,ccy,bsz/2,rgb(250,208,60)); fill_round(bx+2,ccy,10,7,2,15,rgb(236,182,44));
    fill_circle(ccx+5,ccy-4,3,rgb(30,30,34)); fill_rect(bx+bsz-1,ccy-2,7,5,rgb(240,132,40));
    if(fb_state==2){ dstr_ctr(ox+bw/2, oy+bh/2-12, "Game over", rgb(255,255,255),3); dstr_ctr(ox+bw/2, oy+bh/2+22, "space / up to fly again", rgb(20,40,70),1); }
    else dstr_ctr(WN[i].x+WN[i].w/2, oy+bh+12, "Space or Up to flap", rgb(120,128,146),1);
}
/* ---- neofetch (graphical) ---- */

static void nf_line(int x,int*y,const char*label,const char*val){
    dstr(x,*y,label,rgb(60,150,230),1); int lw=slen(label)*8; dstr(x+lw,*y,val,rgb(52,60,78),1); *y+=22;
}
void neofetch_draw(int i){
    win_frame(i,"neofetch");
    int cx=WN[i].x+WN[i].w/2, top=WN[i].y+TITLEH+18;
    (void)cx;
    static const char* NFART[6]={
    "  _   _                           _____ _____ ",
    " | \\ | |                         |  _  /  ___|",
    " |  \\| | ___   _____   _______  _| | | \\ `--. ",
    " | . ` |/ _ \\ / _ \\ \\ / / _ \\ \\/ / | | |`--. \\\\",
    " | |\\  | (_) | (_) \\ V /  __/>  <\\ \\_/ /\\__/ /",
    " \\_| \\_/\\___/ \\___/ \\_/ \\___/_/\\_\\\\___/\\____/"
};
    int x=WN[i].x+24, ay=top;
    for(int l=0;l<6;l++){ dstr(x, ay, NFART[l], rgb(57,196,106), 1); ay+=14; }
    int y=ay+18;
    { char b[48],*p=b; p=scopy(p,g_user);*p++='@';p=scopy(p,"noovex64");*p=0; dstr(x,y,b,ACC,2); } y+=26;
    { int w=WN[i].w-80; for(int k=0;k<w/8;k++)dchar(x+k*8,y,'-',rgb(150,158,176),1); } y+=20;
    char v[80],*p;
    nf_line(x,&y,"OS: ","NoovexOS 64 v9.82 (x86-64)");
    nf_line(x,&y,"Kernel: ","NoovexOS monolithic x86_64");
    nf_line(x,&y,"Build: ","v9.82");
    nf_line(x,&y,"Firmware: ","BIOS / VESA LFB");
    { u32 up=(u32)(g_ticks/100); p=v; p=udec(p,up/3600);*p++='h';*p++=' ';p=udec(p,(up/60)%60);*p++='m';*p++=' ';p=udec(p,up%60);*p++='s';*p=0; nf_line(x,&y,"Uptime: ",v); }
    { p=v; { const char*b=g_brand[0]?g_brand:"x86-64 CPU"; while(*b)*p++=*b++; } *p=0; nf_line(x,&y,"CPU: ",v); }
    { u32 a,bb,c,d; cpuid(1,&a,&bb,&c,&d); int th=(bb>>16)&0xFF; if(th<1)th=1; p=v; p=udec(p,(u32)th); p=scopy(p," logical (CPUID)"); *p=0; nf_line(x,&y,"Threads: ",v); }
    { p=v; p=udec(p,W);*p++='x';p=udec(p,H);p=scopy(p," @ ");p=udec(p,BPP);p=scopy(p,"bpp");*p=0; nf_line(x,&y,"Display: ",v); }
    { p=v; p=udec(p,(u32)((u64)W*H*4/1024));p=scopy(p," KiB (framebuffer)");*p=0; nf_line(x,&y,"Video mem: ",v); }
    { int k=nic_index(); p=v; if(k>=0){ const char*n=nic_name(pci_ven[k],pci_dev[k]); while(*n)*p++=*n++; p=scopy(p, nic_up()?" - link up":" - no link"); } else p=scopy(p,"none detected"); *p=0; nf_line(x,&y,"Network: ",v); }
    { p=v; if(nic_up()){ p=udec(p,(my_ip>>24)&0xFF);*p++='.';p=udec(p,(my_ip>>16)&0xFF);*p++='.';p=udec(p,(my_ip>>8)&0xFF);*p++='.';p=udec(p,my_ip&0xFF);} else p=scopy(p,"-"); *p=0; nf_line(x,&y,"Address: ",v); }
    nf_line(x,&y,"Disk: ","NVXFS (in-RAM)");
    { p=v; p=scopy(p,"long mode active, EFER.LMA="); *p++=(char)('0'+g_lma); *p=0; nf_line(x,&y,"State: ",v); }
    y+=8; u32 pal[8]={rgb(40,46,64),rgb(228,76,74),rgb(57,196,106),rgb(230,190,60),rgb(60,120,224),rgb(180,90,210),rgb(40,180,190),rgb(235,238,245)};
    for(int k=0;k<8;k++) fill_round(x+k*26, y, 22, 16, 3,15, pal[k]);
}
/* ---- Paint ---- */
#define PCW 540
#define PCH 360
static u32 paint_buf[PCW*PCH]; int paint_col=6, paint_size=1, paint_inited=0, pcx,pcy;
int pt_colx[12], pt_coly[12], pt_bx[3], pt_by, pt_clrx, pt_clry, pt_clrw, pt_clrh;
static const u32 PPAL[12]={rgb(20,22,28),rgb(120,128,146),rgb(228,76,74),rgb(230,140,40),rgb(240,200,50),rgb(90,200,110),rgb(50,120,224),rgb(150,90,220),rgb(40,180,190),rgb(230,120,180),rgb(120,80,40),rgb(255,255,255)};
void paint_clear(void){ for(int i=0;i<PCW*PCH;i++)paint_buf[i]=rgb(255,255,255); }
void paint_at(int px,int py){
    int bx=px-pcx, by=py-pcy; if(bx<0||by<0||bx>=PCW||by>=PCH) return;
    int R=(paint_size==0)?2:(paint_size==1)?4:8; u32 c=PPAL[paint_col];
    for(int dy=-R;dy<=R;dy++)for(int dx=-R;dx<=R;dx++){ if(dx*dx+dy*dy>R*R)continue; int x=bx+dx,y=by+dy; if(x<0||y<0||x>=PCW||y>=PCH)continue; paint_buf[y*PCW+x]=c; }
}
void paint_draw(int i){
    win_frame(i,"Paint");
    if(!paint_inited){ paint_clear(); paint_inited=1; }
    int wx=WN[i].x, wy=WN[i].y, tx=wx+10, ty=wy+TITLEH+12;
    for(int k=0;k<12;k++){ int cxp=tx+(k%2)*24, cyp=ty+(k/2)*24; pt_colx[k]=cxp; pt_coly[k]=cyp;
        fill_round(cxp,cyp,20,20,4,15,PPAL[k]);
        if(k==paint_col){ for(int t=0;t<20;t++){putpx(cxp+t,cyp-1,ACC);putpx(cxp+t,cyp+20,ACC);} for(int t=-1;t<21;t++){putpx(cxp-1,cyp+t,ACC);putpx(cxp+20,cyp+t,ACC);} } }
    int by=ty+6*24+16; pt_by=by;
    for(int k=0;k<3;k++){ int cyp=by+k*26; pt_bx[k]=tx; int r=(k==0)?2:(k==1)?4:7;
        if(k==paint_size) fill_round(tx,cyp-11,44,22,5,15,mix(ACC,rgb(255,255,255),204));
        fill_circle(tx+22,cyp,r,rgb(40,46,64)); }
    int cly=by+3*26+8; pt_clrx=tx; pt_clry=cly; pt_clrw=44; pt_clrh=26; fill_round(tx,cly,44,26,6,15,rgb(228,232,240)); dstr_ctr(tx+22,cly+8,"Clr",INK,1);
    pcx=wx+64; pcy=wy+TITLEH+12;
    fill_rect(pcx-2,pcy-2,PCW+4,PCH+4,rgb(198,204,218));
    for(int y=0;y<PCH;y++){ int dy=pcy+y; if(dy<0||dy>=(int)H)continue; u32* srow=&paint_buf[y*PCW]; u32* drow=&SCENE[(u64)dy*SW];
        for(int x=0;x<PCW;x++){ int dx=pcx+x; if(dx<0||dx>=(int)W)continue; drow[dx]=srow[x]; } }
}
