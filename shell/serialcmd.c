#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
int usbser_scan(void); int usbser_info(char* out,int cap); int usbser_find(int acm,int num); int usbser_ok(int k);
int usbser_set_line(int k,u32 baud,int bits,int parity,int stop); int usbser_get_line(int k,u32* baud,int* bits,int* parity,int* stop);
int usbser_mctl(int k,int dtr,int rts); int usbser_break(int k,int on); int usbser_modem(int k);
int usbser_read(int k,u8* out,int max); int usbser_write(int k,const u8* s,int len); void usbser_drop(int k);
extern volatile unsigned char kdown[128];
static int sc_word(const char** p,char* w,int cap){ const char* s=*p; while(*s==' ')s++; int n=0; while(*s&&*s!=' '){ if(n<cap-1)w[n++]=*s; s++; } w[n]=0; while(*s==' ')s++; *p=s; return n; }
static int sc_eq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static int sc_num(const char* s,u32* v){ u32 n=0; int d=0; while(*s>='0'&&*s<='9'&&d<9){ n=n*10+(u32)(*s-'0'); s++; d++; } if(*s||!d)return 0; *v=n; return 1; }
static int sc_dev(const char* w){
    const char* s=w; if(s[0]=='/'&&s[1]=='d'&&s[2]=='e'&&s[3]=='v'&&s[4]=='/')s+=5;
    int acm; if(s[0]=='t'&&s[1]=='t'&&s[2]=='y'&&s[3]=='U'&&s[4]=='S'&&s[5]=='B')acm=0; else if(s[0]=='t'&&s[1]=='t'&&s[2]=='y'&&s[3]=='A'&&s[4]=='C'&&s[5]=='M')acm=1; else return -2;
    u32 n; if(!sc_num(s+6,&n)||n>15)return -2;
    int k=usbser_find(acm,(int)n); if(k<0){ usbser_scan(); k=usbser_find(acm,(int)n); }
    return k; }
static void sc_line(int k){ u32 b; int bits,par,st; if(usbser_get_line(k,&b,&bits,&par,&st))return; char t[96],*p=t; p=udec(p,b); *p++=' '; *p++=(char)('0'+bits); *p++="NOEMS"[par%5]; *p++=st==2?'2':'1';
    int m=usbser_modem(k); if(m>=0){ p=scopy(p,(m&2)?"  DTR on":"  DTR off"); p=scopy(p,(m&4)?"  RTS on":"  RTS off"); p=scopy(p,(m&0x20)?"  CTS":"  cts"); p=scopy(p,(m&0x100)?" DSR":" dsr"); p=scopy(p,(m&0x40)?" CD":" cd"); }
    *p++='\n'; *p=0; term_puts(t); }
static int sc_fmt(const char* w,int* bits,int* par,int* st){ if(w[0]<'5'||w[0]>'8'||!w[1]||!w[2]||w[3])return 0; const char* P="NOEMS"; int q=-1; for(int i=0;i<5;i++) if((w[1]&~32)==P[i])q=i; if(q<0||(w[2]!='1'&&w[2]!='2'))return 0; *bits=w[0]-'0'; *par=q; *st=w[2]=='2'?2:0; return 1; }
static void sc_monitor(int k,u32 secs){
    term_puts("serial monitor - press Esc to stop\n");
    u64 t0=hr_ns(), lim=(u64)secs*1000000000ULL; char o[260];
    while(hr_ns()-t0<lim&&!kdown[0x01]&&usbser_ok(k)){ u8 m[256]; int n=usbser_read(k,m,255); if(n<=0){ hr_delay_us(2000); continue; }
        int j=0; for(int i=0;i<n;i++){ u8 c=m[i]; if(c=='\r')continue; o[j++]=(c=='\n'||c=='\t'||(c>=32&&c<127))?(char)c:'.'; } o[j]=0; term_puts(o); }
    term_putc('\n'); if(!usbser_ok(k))term_puts("adapter removed\n"); }
void shell_serial(const char* arg){
    char w[48], b[900]; const char* a=arg?arg:"";
    if(!sc_word(&a,w,(int)sizeof w)||sc_eq(w,"list")||sc_eq(w,"scan")){ usbser_scan(); usbser_info(b,(int)sizeof b); term_puts(b); term_putc('\n'); return; }
    int k=sc_dev(w);
    if(k==-2){ term_puts("usage: serial [list] | serial ttyUSB0 [baud] [8N1] | monitor [baud] | send <text> | dtr on|off | rts on|off | break | reset esp32|arduino\n"); return; }
    if(k<0){ term_puts("no such serial port (try: serial list)\n"); return; }
    char c[24]; sc_word(&a,c,(int)sizeof c); u32 v;
    if(!c[0]){ sc_line(k); return; }
    if(sc_num(c,&v)||sc_eq(c,"monitor")){ int mon=sc_eq(c,"monitor"); if(mon){ char d[16]; if(sc_word(&a,d,(int)sizeof d)&&!sc_num(d,&v)){ term_puts("bad baud rate\n"); return; } if(!d[0])v=0; }
        u32 ob; int bits,par,st; usbser_get_line(k,&ob,&bits,&par,&st); char f[8]; if(sc_word(&a,f,(int)sizeof f)&&!sc_fmt(f,&bits,&par,&st)){ term_puts("format is like 8N1, 7E1, 8N2\n"); return; }
        if(v){ int e=usbser_set_line(k,v,bits,par,st); if(e){ term_puts(e==-2?"this adapter cannot use that baud rate or format\n":"adapter did not accept the setting\n"); return; } }
        if(mon){ usbser_drop(k); sc_monitor(k,600); } else sc_line(k); return; }
    if(sc_eq(c,"send")){ int n=0; while(a[n]&&n<800){ b[n]=a[n]; n++; } b[n++]='\r'; b[n++]='\n'; int r=usbser_write(k,(const u8*)b,n); term_puts(r==n?"sent\n":"send failed\n"); return; }
    if(sc_eq(c,"dtr")||sc_eq(c,"rts")){ char d[8]; sc_word(&a,d,(int)sizeof d); int on=sc_eq(d,"on")||sc_eq(d,"1"); if(!on&&!sc_eq(d,"off")&&!sc_eq(d,"0")){ term_puts("use on or off\n"); return; }
        int m=usbser_modem(k); if(m<0)m=0; int dtr=(m&2)!=0, rts=(m&4)!=0; if(c[0]=='d')dtr=on; else rts=on;
        term_puts(usbser_mctl(k,dtr,rts)?"adapter did not accept the change\n":"ok\n"); return; }
    if(sc_eq(c,"break")){ if(usbser_break(k,1)){ term_puts("break not supported\n"); return; } hr_delay_us(250000); usbser_break(k,0); term_puts("break sent\n"); return; }
    if(sc_eq(c,"reset")){ char d[16]; sc_word(&a,d,(int)sizeof d);
        if(sc_eq(d,"esp32")||sc_eq(d,"esp")){ int e=usbser_mctl(k,0,1); hr_delay_us(100000); e|=usbser_mctl(k,1,0); hr_delay_us(50000); e|=usbser_mctl(k,0,0); term_puts(e?"reset failed\n":"ESP32 reset into the ROM bootloader (DTR/RTS)\n"); return; }
        if(sc_eq(d,"arduino")||!d[0]){ int e=usbser_mctl(k,0,0); hr_delay_us(250000); e|=usbser_mctl(k,1,1); hr_delay_us(50000); term_puts(e?"reset failed\n":"board reset (DTR pulse)\n"); return; }
        term_puts("usage: serial ttyUSB0 reset esp32|arduino\n"); return; }
    term_puts("unknown serial command\n"); }
