#include "include/nvx/base.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/shell/vt.h"
#include "include/nvx/core/proc/jobs.h"

struct vt_tios g_tios;
int g_vt_curhide=0, g_vt_appcur=0, g_vt_alt=0;

static int st=0, np=0, priv=0; static int pr[16]; static int u8n=0; static u32 u8cp=0;
static u32 fg=WHT, bg=0; static int bold=0, rev=0, wrap=1, top=0, bot=-1;
static int svx=0, svy=0; static u32 svfg=WHT, svbg=0; static int svbold=0, svrev=0;
static char* altch=0; static u32* altco=0; static u32* altbg=0; static int altx=0, alty=0;
static char lastc=' ';

static const u32 PAL16[16]={0x000000,0xCD3131,0x0DBC79,0xE5E510,0x2472C8,0xBC3FBC,0x11A8CD,0xE5E5E5,
                            0x666666,0xF14C4C,0x23D18B,0xF5F543,0x3B8EEA,0xD670D6,0x29B8DB,0xFFFFFF};
static u32 pal256(int n){
    if(n<0)n=0; if(n>255)n=255;
    if(n<16)return PAL16[n];
    if(n<232){ n-=16; static const u8 L[6]={0,95,135,175,215,255}; return rgb(L[n/36],L[(n/6)%6],L[n%6]); }
    int g=8+10*(n-232); return rgb(g,g,g);
}

void vt_tios_default(void){
    u8* z=(u8*)&g_tios; for(int i=0;i<(int)sizeof g_tios;i++)z[i]=0;
    g_tios.iflag=0x500; g_tios.oflag=TIO_OPOST|TIO_ONLCR; g_tios.cflag=0xBF; g_tios.lflag=0x8A3B;
    u8* cc=g_tios.cc; cc[0]=3; cc[1]=28; cc[2]=127; cc[3]=21; cc[4]=4; cc[6]=1; cc[8]=17; cc[9]=19; cc[10]=26; cc[12]=18; cc[13]=15; cc[14]=23; cc[15]=22;
}
int vt_raw(void){ if(!g_tios.lflag&&!g_tios.oflag&&!g_tios.cflag)vt_tios_default(); return !(g_tios.lflag&TIO_ICANON); }

static int B(void){ return (bot<0||bot>=trows)?trows-1:bot; }
static u32 efg(void){ u32 f=fg; if(bold&&f!=WHT){ for(int i=0;i<8;i++) if(f==PAL16[i]){ f=PAL16[i+8]; break; } } if(rev)return bg?(bg&0xFFFFFF):rgb(12,14,20); return f; }
static u32 ebg(void){ if(rev)return (fg|VT_BGSET); return bg; }
static void clear_cell(int r,int c){ TCH[r][c]=' '; TCO[r][c]=WHT; TBG[r][c]=bg; }
static void clear_row(int r,int c0,int c1){ if(r<0||r>=trows)return; if(c0<0)c0=0; if(c1>tcols)c1=tcols; for(int c=c0;c<c1;c++)clear_cell(r,c); }
static void copy_row(int d,int s){ for(int c=0;c<TCMAX;c++){ TCH[d][c]=TCH[s][c]; TCO[d][c]=TCO[s][c]; TBG[d][c]=TBG[s][c]; } }
static void scroll_up(int t,int b,int n){
    if(t==0&&b==trows-1&&!g_vt_alt&&n==1){ term_scroll_up(); clear_row(trows-1,0,tcols); return; }
    for(int k=0;k<n;k++){ for(int r=t;r<b;r++)copy_row(r,r+1); clear_row(b,0,tcols); }
}
static void scroll_dn(int t,int b,int n){ for(int k=0;k<n;k++){ for(int r=b;r>t;r--)copy_row(r,r-1); clear_row(t,0,tcols); } }
static void lf(void){ int b=B(); if(tcy==b)scroll_up(top,b,1); else if(tcy<trows-1)tcy++; }
static void put(char ch){
    if(tcx>=tcols){ if(wrap){ tcx=0; lf(); } else tcx=tcols-1; }
    TCH[tcy][tcx]=ch; TCO[tcy][tcx]=efg(); TBG[tcy][tcx]=ebg(); tcx++; lastc=ch;
}
static void clampc(void){ if(tcx<0)tcx=0; if(tcx>=tcols)tcx=tcols-1; if(tcy<0)tcy=0; if(tcy>=trows)tcy=trows-1; }
static int P(int i,int d){ return (i<np&&pr[i]>0)?pr[i]:d; }
static void alt_enter(void){
    if(g_vt_alt)return; int n=TRMAX*TCMAX;
    if(!altch){ altch=(char*)kmalloc((unsigned long)n); altco=(u32*)kmalloc((unsigned long)n*4); altbg=(u32*)kmalloc((unsigned long)n*4); if(!altch||!altco||!altbg)return; }
    for(int r=0;r<TRMAX;r++)for(int c=0;c<TCMAX;c++){ altch[r*TCMAX+c]=TCH[r][c]; altco[r*TCMAX+c]=TCO[r][c]; altbg[r*TCMAX+c]=TBG[r][c]; }
    altx=tcx; alty=tcy; g_vt_alt=1;
    for(int r=0;r<trows;r++)clear_row(r,0,tcols);
}
static void alt_leave(void){
    if(!g_vt_alt)return; g_vt_alt=0;
    for(int r=0;r<TRMAX;r++)for(int c=0;c<TCMAX;c++){ TCH[r][c]=altch[r*TCMAX+c]; TCO[r][c]=altco[r*TCMAX+c]; TBG[r][c]=altbg[r*TCMAX+c]; }
    tcx=altx; tcy=alty;
}
static void sgr(void){
    if(!np){ fg=WHT; bg=0; bold=0; rev=0; return; }
    for(int i=0;i<np;i++){ int v=pr[i]<0?0:pr[i];
        if(v==0){ fg=WHT; bg=0; bold=0; rev=0; }
        else if(v==1)bold=1; else if(v==22||v==2)bold=0; else if(v==7)rev=1; else if(v==27)rev=0;
        else if(v>=30&&v<=37)fg=PAL16[v-30]; else if(v>=90&&v<=97)fg=PAL16[v-90+8]; else if(v==39)fg=WHT;
        else if(v>=40&&v<=47)bg=PAL16[v-40]|VT_BGSET; else if(v>=100&&v<=107)bg=PAL16[v-100+8]|VT_BGSET; else if(v==49)bg=0;
        else if((v==38||v==48)&&i+1<np){ u32 c; if(pr[i+1]==5&&i+2<np){ c=pal256(pr[i+2]); i+=2; } else if(pr[i+1]==2&&i+4<np){ c=rgb(pr[i+2]&255,pr[i+3]&255,pr[i+4]&255); i+=4; } else { i=np; break; }
            if(v==38)fg=c; else bg=c|VT_BGSET; } }
}
void vt_reset(void){
    alt_leave(); fg=WHT; bg=0; bold=0; rev=0; wrap=1; top=0; bot=-1; g_vt_curhide=0; g_vt_appcur=0; st=0; u8n=0; vt_tios_default();
}
static void csi(char f){
    int b=B();
    if(priv){
        if(f=='h'||f=='l'){ int on=f=='h'; for(int i=0;i<np;i++){ int v=pr[i];
            if(v==25)g_vt_curhide=!on; else if(v==1)g_vt_appcur=on; else if(v==7)wrap=on;
            else if(v==1049||v==47||v==1047){ if(on)alt_enter(); else alt_leave(); } } }
        return; }
    switch(f){
    case 'A': tcy-=P(0,1); if(tcy<top&&tcy+P(0,1)>=top)tcy=top; clampc(); break;
    case 'B': tcy+=P(0,1); if(tcy>b&&tcy-P(0,1)<=b)tcy=b; clampc(); break;
    case 'C': case 'a': tcx+=P(0,1); clampc(); break;
    case 'D': if(tcx>=tcols)tcx=tcols-1; tcx-=P(0,1); clampc(); break;
    case 'E': tcy+=P(0,1); tcx=0; clampc(); break;
    case 'F': tcy-=P(0,1); tcx=0; clampc(); break;
    case 'G': case '`': tcx=P(0,1)-1; clampc(); break;
    case 'd': tcy=P(0,1)-1; clampc(); break;
    case 'H': case 'f': tcy=P(0,1)-1; tcx=P(1,1)-1; clampc(); break;
    case 'J': { int m=np?pr[0]:0; if(m<0)m=0; if(tcx>=tcols)tcx=tcols-1;
        if(m==0){ clear_row(tcy,tcx,tcols); for(int r=tcy+1;r<trows;r++)clear_row(r,0,tcols); }
        else if(m==1){ clear_row(tcy,0,tcx+1); for(int r=0;r<tcy;r++)clear_row(r,0,tcols); }
        else { for(int r=0;r<trows;r++)clear_row(r,0,tcols); }
        break; }
    case 'K': { int m=np?pr[0]:0; if(m<0)m=0; int x=tcx>=tcols?tcols-1:tcx;
        if(m==0)clear_row(tcy,x,tcols); else if(m==1)clear_row(tcy,0,x+1); else clear_row(tcy,0,tcols); break; }
    case 'L': if(tcy>=top&&tcy<=b)scroll_dn(tcy,b,P(0,1)>b-tcy+1?b-tcy+1:P(0,1)); tcx=0; break;
    case 'M': if(tcy>=top&&tcy<=b)scroll_up(tcy,b,P(0,1)>b-tcy+1?b-tcy+1:P(0,1)); tcx=0; break;
    case 'P': { int n=P(0,1), x=tcx>=tcols?tcols-1:tcx; if(n>tcols-x)n=tcols-x;
        for(int c=x;c<tcols-n;c++){ TCH[tcy][c]=TCH[tcy][c+n]; TCO[tcy][c]=TCO[tcy][c+n]; TBG[tcy][c]=TBG[tcy][c+n]; } clear_row(tcy,tcols-n,tcols); break; }
    case '@': { int n=P(0,1), x=tcx>=tcols?tcols-1:tcx; if(n>tcols-x)n=tcols-x;
        for(int c=tcols-1;c>=x+n;c--){ TCH[tcy][c]=TCH[tcy][c-n]; TCO[tcy][c]=TCO[tcy][c-n]; TBG[tcy][c]=TBG[tcy][c-n]; } clear_row(tcy,x,x+n); break; }
    case 'X': { int x=tcx>=tcols?tcols-1:tcx; clear_row(tcy,x,x+P(0,1)); break; }
    case 'S': scroll_up(top,b,P(0,1)); break;
    case 'T': scroll_dn(top,b,P(0,1)); break;
    case 'b': { int n=P(0,1); if(n>1000)n=1000; for(int i=0;i<n;i++)put(lastc); break; }
    case 'm': sgr(); break;
    case 'r': { int t=P(0,1)-1, bb=P(1,trows)-1; if(t<0)t=0; if(bb>=trows)bb=trows-1; if(t<bb){ top=t; bot=bb; } else { top=0; bot=-1; } tcx=0; tcy=0; break; }
    case 's': svx=tcx; svy=tcy; break;
    case 'u': tcx=svx; tcy=svy; clampc(); break;
    case 'n': if(np&&pr[0]==6){ char rb[32]; char* q=rb; *q++=27; *q++='['; int v=tcy+1; char t[8]; int k=0; do{ t[k++]=(char)('0'+v%10); v/=10; }while(v); while(k)*q++=t[--k];
            *q++=';'; v=(tcx>=tcols?tcols-1:tcx)+1; do{ t[k++]=(char)('0'+v%10); v/=10; }while(v); while(k)*q++=t[--k]; *q++='R'; *q=0; vt_reply(rb); }
        else if(np&&pr[0]==5)vt_reply("\033[0n");
        break;
    case 'c': vt_reply("\033[?1;2c"); break;
    }
}
static void esc(char c){
    if(c=='7'){ svx=tcx; svy=tcy; svfg=fg; svbg=bg; svbold=bold; svrev=rev; }
    else if(c=='8'){ tcx=svx; tcy=svy; fg=svfg; bg=svbg; bold=svbold; rev=svrev; clampc(); }
    else if(c=='M'){ if(tcy==top)scroll_dn(top,B(),1); else if(tcy>0)tcy--; }
    else if(c=='D')lf();
    else if(c=='E'){ tcx=0; lf(); }
    else if(c=='c'){ vt_reset(); for(int r=0;r<trows;r++)clear_row(r,0,tcols); tcx=tcy=0; }
}
static char u8ascii(u32 cp){
    if(cp==0x2500||cp==0x2501||cp==0x2504||cp==0x2505||cp==0x2508||cp==0x2509||cp==0x254C||cp==0x254D||cp==0x2550||cp==0x2574||cp==0x2576||cp==0x2578||cp==0x257A)return '-';
    if(cp==0x2502||cp==0x2503||cp==0x2506||cp==0x2507||cp==0x250A||cp==0x250B||cp==0x254E||cp==0x254F||cp==0x2551||cp==0x2575||cp==0x2577||cp==0x2579||cp==0x257B)return '|';
    if(cp>=0x2500&&cp<=0x257F)return '+';
    if(cp>=0x2580&&cp<=0x259F)return '#';
    if(cp==0x2018||cp==0x2019)return '\'';
    if(cp==0x201C||cp==0x201D)return '"';
    if(cp==0x2013||cp==0x2014)return '-';
    if(cp==0x2026)return '.';
    if(cp==0x2022||cp==0xB7)return '*';
    if(cp==0xA0)return ' ';
    return '?';
}
void vt_out(const char* s,int n){
    if(bot>=trows)bot=-1; if(top>=trows)top=0;
    if(tcy>=trows)tcy=trows-1;
    for(int i=0;i<n;i++){ unsigned char c=(unsigned char)s[i];
        if(st==0&&u8n){ if((c&0xC0)==0x80){ u8cp=(u8cp<<6)|(c&0x3F); if(--u8n==0)put(u8ascii(u8cp)); continue; } u8n=0; put('?'); }
        if(st==0&&c>=0x80){ if((c&0xE0)==0xC0){ u8n=1; u8cp=c&0x1F; } else if((c&0xF0)==0xE0){ u8n=2; u8cp=c&0x0F; } else if((c&0xF8)==0xF0){ u8n=3; u8cp=c&0x07; } else put('?'); continue; }
        if(c==27&&st!=3){ st=1; continue; }
        if(st==1){ st=0;
            if(c=='['){ st=2; np=0; priv=0; pr[0]=-1; continue; }
            if(c==']'){ st=3; continue; }
            if(c=='('||c==')'||c=='*'||c=='+'||c=='#'||c=='%'){ st=4; continue; }
            esc((char)c); continue; }
        if(st==4){ st=0; continue; }
        if(st==3){ if(c==7)st=0; else if(c==27)st=5; continue; }
        if(st==5){ st=0; continue; }
        if(st==2){
            if(c>='0'&&c<='9'){ if(np==0)np=1; if(pr[np-1]<0)pr[np-1]=0; if(pr[np-1]<100000)pr[np-1]=pr[np-1]*10+(c-'0'); continue; }
            if(c==';'||c==':'){ if(np==0){ np=1; pr[0]=-1; } if(np<16){ pr[np]=-1; np++; } continue; }
            if(c=='?'||c=='>'||c=='='||c=='<'){ priv=1; continue; }
            if(c>=0x20&&c<0x40)continue;
            if(c<0x20){ goto ctl; }
            st=0; csi((char)c); continue; }
    ctl:
        if(c=='\n'){ if((g_tios.oflag&(TIO_OPOST|TIO_ONLCR))==(TIO_OPOST|TIO_ONLCR)||!g_tios.oflag)tcx=0; lf(); continue; }
        if(c=='\r'){ tcx=0; continue; }
        if(c=='\b'){ if(tcx>=tcols)tcx=tcols-1; if(tcx>0)tcx--; continue; }
        if(c=='\t'){ int nx=(tcx/8+1)*8; if(nx>=tcols)nx=tcols-1; while(tcx<nx)put(' '); continue; }
        if(c==11||c==12){ lf(); continue; }
        if(c<32||c==127)continue;
        put((char)c);
    }
    g_term_gen++;
}
void vt_capture(const char* s,int n){
    static int cst=0, cn=0; static u32 ccp=0;
    for(int i=0;i<n;i++){ unsigned char c=(unsigned char)s[i];
        if(cst==1){ cst=(c=='[')?2:(c==']')?3:0; continue; }
        if(cst==2){ if(c>=0x40&&c<=0x7E)cst=0; continue; }
        if(cst==3){ if(c==7)cst=0; else if(c==27)cst=0; continue; }
        if(cn){ if((c&0xC0)==0x80){ ccp=(ccp<<6)|(c&0x3F); if(--cn==0)term_putc(u8ascii(ccp)); continue; } cn=0; term_putc('?'); }
        if(c>=0x80){ if((c&0xE0)==0xC0){ cn=1; ccp=c&0x1F; } else if((c&0xF0)==0xE0){ cn=2; ccp=c&0x0F; } else if((c&0xF8)==0xF0){ cn=3; ccp=c&0x07; } else term_putc('?'); continue; }
        if(c==27){ cst=1; continue; }
        if(c=='\r')continue;
        if(c=='\t'){ term_putc(' '); term_putc(' '); continue; }
        if(c<32&&c!='\n'&&c!='\b')continue;
        term_putc((char)c); }
}
