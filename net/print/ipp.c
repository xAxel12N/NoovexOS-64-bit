#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/assets/fontaa.h"
#include "include/nvx/net/stack/sockets.h"

#define PW 2480
#define PH 3508
#define PMARG 150
#define LINEH 48
u32 g_prn_ip=0; char g_prn_model[96]; int g_prn_pwg=-1, g_prn_urf=0, g_prn_pdf=0; char g_prn_status[96]="No printer set"; int g_prn_pages=0; u32 g_prn_bytes=0;
extern int parse_ip(const char*s,u32*out);
extern int sock_send_rel(int sid,const u8* buf,int len);

static u8* pb; static u32 pn, pcap;
static void put8(u8 v){ if(pn<pcap)pb[pn++]=v; }
static void put16(u16 v){ put8((u8)(v>>8)); put8((u8)v); }
static void put32(u32 v){ put16((u16)(v>>16)); put16((u16)v); }
static void putn(const void* s,u32 n){ const u8* q=(const u8*)s; for(u32 i=0;i<n;i++)put8(q[i]); }
static void be32at(u8* h,int off,u32 v){ h[off]=(u8)(v>>24); h[off+1]=(u8)(v>>16); h[off+2]=(u8)(v>>8); h[off+3]=(u8)v; }
static void strat(u8* h,int off,const char* s){ for(int i=0;s[i]&&i<63;i++)h[off+i]=(u8)s[i]; }
static void ipp_attr(u8 tag,const char* name,const char* val){ u16 nl=0; while(name[nl])nl++; u16 vl=0; while(val[vl])vl++; put8(tag); put16(nl); putn(name,nl); put16(vl); putn(val,vl); }

static int line_count(const char* t,int cols){ int n=0, c=0; for(int i=0;t[i];i++){ if(t[i]=='\n'||c>=cols){ n++; c=0; if(t[i]=='\n')continue; } c++; } return n+1; }
static const char* line_at(const char* t,int cols,int li,int* len){ int n=0, c=0, st=0; for(int i=0;;i++){ char ch=t[i]; if(!ch||ch=='\n'||c>=cols){ if(n==li){ *len=i-st; return t+st; } if(!ch){ *len=0; return t; } n++; c=0; st=(ch=='\n')?i+1:i; if(ch=='\n')continue; } c++; } }

static void page_header(int page,int total){
    u8 h[1796]; for(int i=0;i<1796;i++)h[i]=0;
    strat(h,0,"PwgRaster"); strat(h,64,""); strat(h,128,"stationery"); strat(h,192,"");
    be32at(h,276,300); be32at(h,280,300); be32at(h,340,1); be32at(h,352,595); be32at(h,356,842);
    be32at(h,372,PW); be32at(h,376,PH); be32at(h,384,8); be32at(h,388,8); be32at(h,392,PW); be32at(h,400,18); be32at(h,420,1);
    be32at(h,452,(u32)total); be32at(h,456,1); be32at(h,460,1); be32at(h,480,0xFFFFFFu);
    strat(h,1732,"iso_a4_210x297mm"); (void)page; putn(h,1796); }
static void enc_row(const u8* r){
    int x=0; while(x<PW){ int run=1; while(x+run<PW&&run<128&&r[x+run]==r[x])run++;
        if(run>=2){ put8((u8)(run-1)); put8(r[x]); x+=run; continue; }
        int st=x, lit=0; while(x<PW&&lit<128){ if(x+1<PW&&r[x+1]==r[x])break; x++; lit++; }
        if(lit==0){ lit=1; x++; } put8((u8)(257-lit)); putn(r+st,(u32)lit); } }
static void render_row(u8* row,const char* text,int cols,int page,int y,int lines_per){
    for(int i=0;i<PW;i++)row[i]=255;
    if(y<PMARG||y>=PH-PMARG)return; int ly=y-PMARG; int li=ly/LINEH, gr=(ly%LINEH)/3; if(gr>=14||li>=lines_per)return;
    int len=0; const char* s=line_at(text,cols,page*lines_per+li,&len);
    for(int c=0;c<len&&c<cols;c++){ int idx=(u8)s[c]-32; if(idx<0||idx>=95)continue; const unsigned char* g=fontaa[idx]+gr*8;
        for(int col=0;col<8;col++){ int a=g[col]; if(a<8)continue; u8 v=(u8)(255-a); int px=PMARG+c*24+col*3; row[px]=v; row[px+1]=v; row[px+2]=v; } } }

int prn_print_text(const char* title,const char* text){
    if(!g_prn_ip){ scopy(g_prn_status,"No printer set - use: printer <ip address>")[0]=0; return -1; }
    int cols=(PW-2*PMARG)/24, lines_per=(PH-2*PMARG)/LINEH; int nl=line_count(text,cols); int pages=(nl+lines_per-1)/lines_per; if(pages<1)pages=1; if(pages>30)pages=30;
    pcap=600000u+(u32)pages*900000u; pb=(u8*)kmalloc(pcap); if(!pb){ scopy(g_prn_status,"Not enough memory for the print job")[0]=0; return -2; }
    pn=0; char ipstr[20],*q=ipstr; for(int k=3;k>=0;k--){ q=udec(q,(g_prn_ip>>(8*(3-k)))&0xFF); if(k)*q++='.'; } *q=0;
    char uri[64],*u=uri; u=scopy(u,"ipp://"); u=scopy(u,ipstr); u=scopy(u,":631/ipp/print"); *u=0;
    u32 hpos=pn; for(int i=0;i<200;i++)put8(' ');
    u32 body=pn;
    put16(0x0200); put16(0x0002); put32(1); put8(0x01);
    ipp_attr(0x47,"attributes-charset","utf-8"); ipp_attr(0x48,"attributes-natural-language","en"); ipp_attr(0x45,"printer-uri",uri);
    ipp_attr(0x42,"requesting-user-name","noovexos"); ipp_attr(0x42,"job-name",title&&title[0]?title:"NoovexOS document"); ipp_attr(0x49,"document-format","image/pwg-raster"); put8(0x03);
    putn("RaS2",4);
    u8* row=(u8*)kmalloc(PW*2); if(!row){ kfree(pb); return -2; } u8* prev=row+PW;
    for(int pg=0;pg<pages;pg++){ page_header(pg,pages); int rep=0; int have=0;
        for(int y=0;y<PH;y++){ render_row(row,text,cols,pg,y,lines_per); int same=have; if(have) for(int i=0;i<PW;i++) if(row[i]!=prev[i]){ same=0; break; }
            if(same&&rep<255){ rep++; continue; }
            if(have){ put8((u8)rep); enc_row(prev); } for(int i=0;i<PW;i++)prev[i]=row[i]; have=1; rep=0; }
        put8((u8)rep); enc_row(prev); }
    kfree(row);
    if(pn>=pcap){ kfree(pb); scopy(g_prn_status,"Print job too large")[0]=0; return -3; }
    u32 blen=pn-body; char hdr[200],*h=hdr; h=scopy(h,"POST /ipp/print HTTP/1.1\r\nHost: "); h=scopy(h,ipstr); h=scopy(h,":631\r\nContent-Type: application/ipp\r\nContent-Length: "); h=udec(h,blen); h=scopy(h,"\r\nConnection: close\r\n\r\n"); int hl=(int)(h-hdr);
    u32 start=body-(u32)hl; for(int i=0;i<hl;i++)pb[start+i]=(u8)hdr[i]; (void)hpos;
    g_prn_bytes=blen; g_prn_pages=pages; scopy(g_prn_status,"Sending to printer...")[0]=0;
    int sid=sock_alloc(); if(sid<0){ kfree(pb); return -4; }
    int r=sock_connect(sid,g_prn_ip,631); if(r){ sock_close(sid); kfree(pb); char* p=g_prn_status; p=scopy(p,"Printer did not answer on port 631 (error "); p=udec(p,(u32)-r); p=scopy(p,")"); *p=0; return -5; }
    if(sock_send_rel(sid,pb+start,(int)(pn-start))<0){ sock_close(sid); kfree(pb); scopy(g_prn_status,"Sending the job failed (no answer from the printer)")[0]=0; return -6; }
    kfree(pb);
    u8 resp[2048]; int got=0; for(int t=0;t<6&&got<(int)sizeof(resp)-1;t++){ int n=sock_recv(sid,resp+got,(int)sizeof(resp)-1-got); if(n<=0)break; got+=n; int done=0; for(int i=0;i+3<got;i++) if(resp[i]=='\r'&&resp[i+1]=='\n'&&resp[i+2]=='\r'&&resp[i+3]=='\n'&&got>=i+12){ done=1; break; } if(done)break; }
    sock_close(sid);
    int he=-1; for(int i=0;i+3<got;i++) if(resp[i]=='\r'&&resp[i+1]=='\n'&&resp[i+2]=='\r'&&resp[i+3]=='\n'){ he=i+4; break; }
    int http=0; if(got>12&&resp[0]=='H'){ http=(resp[9]-'0')*100+(resp[10]-'0')*10+(resp[11]-'0'); }
    char* p=g_prn_status;
    if(he>0&&got>=he+4&&http==200){ u16 st=(u16)((resp[he+2]<<8)|resp[he+3]);
        if(st<0x0100){ p=scopy(p,"Printed "); p=udec(p,(u32)pages); p=scopy(p,pages==1?" page":" pages"); *p=0; klog("printer: job accepted"); return 0; }
        p=scopy(p,"Printer refused the job (IPP status 0x"); p=hx(p,st,4); p=scopy(p,")"); *p=0; return -7; }
    p=scopy(p,"Printer answered HTTP "); p=udec(p,(u32)http); if(!got)p=scopy(p," (no reply)"); *p=0; return -8; }

static int prn_query(const char* ipstr){
    u8 req[600]; pb=req; pn=0; pcap=sizeof(req); for(int i=0;i<200;i++)put8(' '); u32 body=pn;
    char uri[64],*u=uri; u=scopy(u,"ipp://"); u=scopy(u,ipstr); u=scopy(u,":631/ipp/print"); *u=0;
    put16(0x0200); put16(0x000B); put32(2); put8(0x01);
    ipp_attr(0x47,"attributes-charset","utf-8"); ipp_attr(0x48,"attributes-natural-language","en"); ipp_attr(0x45,"printer-uri",uri);
    ipp_attr(0x44,"requested-attributes","printer-make-and-model"); put8(0x44); put16(0); put16(25); putn("document-format-supported",25); put8(0x44); put16(0); put16(13); putn("printer-state",13); put8(0x03);
    u32 blen=pn-body; char hdr[200],*h=hdr; h=scopy(h,"POST /ipp/print HTTP/1.1\r\nHost: "); h=scopy(h,ipstr); h=scopy(h,":631\r\nContent-Type: application/ipp\r\nContent-Length: "); h=udec(h,blen); h=scopy(h,"\r\nConnection: close\r\n\r\n"); int hl=(int)(h-hdr);
    u32 start=body-(u32)hl; for(int i=0;i<hl;i++)req[start+i]=(u8)hdr[i];
    int sid=sock_alloc(); if(sid<0)return -1; if(sock_connect(sid,g_prn_ip,631)){ sock_close(sid); return -2; }
    if(sock_send_rel(sid,req+start,(int)(pn-start))<0){ sock_close(sid); return -3; }
    static u8 rb[16384]; int got=0; for(int t=0;t<40&&got<(int)sizeof(rb);t++){ int n=sock_recv(sid,rb+got,(int)sizeof(rb)-got); if(n<=0)break; got+=n; }
    sock_close(sid);
    int he=-1; for(int i=0;i+3<got;i++) if(rb[i]=='\r'&&rb[i+1]=='\n'&&rb[i+2]=='\r'&&rb[i+3]=='\n'){ he=i+4; break; } if(he<0||got<he+9)return -4;
    int i=he+8; char last[48]; last[0]=0; g_prn_pwg=0; g_prn_urf=0; g_prn_pdf=0;
    while(i<got){ u8 tag=rb[i++]; if(tag==0x03)break; if(tag<0x10)continue; if(i+2>got)break; int nl=(rb[i]<<8)|rb[i+1]; i+=2; if(i+nl>got)break;
        if(nl){ int k=0; for(;k<nl&&k<47;k++)last[k]=(char)rb[i+k]; last[k]=0; } i+=nl; if(i+2>got)break; int vl=(rb[i]<<8)|rb[i+1]; i+=2; if(i+vl>got)break;
        const char* v=(const char*)rb+i;
        if(!nl||1){ int is=1; const char* nm="printer-make-and-model"; for(int k=0;nm[k];k++) if(last[k]!=nm[k]){ is=0; break; } if(is&&last[22]==0){ int k=0; for(;k<vl&&k<95;k++)g_prn_model[k]=v[k]; g_prn_model[k]=0; } }
        if(vl==16&&v[0]=='i'&&v[6]=='p'&&v[7]=='w'&&v[8]=='g')g_prn_pwg=1;
        if(vl==9&&v[6]=='u'&&v[7]=='r'&&v[8]=='f')g_prn_urf=1;
        if(vl==15&&v[12]=='p'&&v[13]=='d'&&v[14]=='f')g_prn_pdf=1;
        i+=vl; }
    return 0; }
int prn_set(const char* ip){ u32 v; if(!parse_ip(ip,&v))return -1; g_prn_ip=v; g_prn_model[0]=0; char* p=g_prn_status;
    int r=prn_query(ip);
    if(r){ p=scopy(p,"Printer "); p=scopy(p,ip); p=scopy(p," did not answer IPP on port 631"); *p=0; return -2; }
    p=scopy(p,g_prn_model[0]?g_prn_model:"IPP printer"); p=scopy(p," at "); p=scopy(p,ip);
    p=scopy(p,g_prn_pwg?" - ready (PWG raster)":g_prn_urf?" - AirPrint only (Apple raster), not supported yet":" - no supported page format"); *p=0; return 0; }
int prn_test_page(void){
    static const char* t="NoovexOS test page\n\nIf you can read this, NoovexOS printed through IPP Everywhere\n(PWG raster, 300 dpi, A4, greyscale).\n\nABCDEFGHIJKLMNOPQRSTUVWXYZ\nabcdefghijklmnopqrstuvwxyz\n0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~\n";
    return prn_print_text("NoovexOS test page",t); }
