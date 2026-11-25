#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
static char OUT[8192]; static int ON;
static u32 ACC=1, GREY=2, CUR=0; static int TITLE_COL_SEEN;
static void term_putc(char c){ if(ON<(int)sizeof OUT-1)OUT[ON++]=c; OUT[ON]=0; }
static void tcol(u32 c){ CUR=c; if(c==ACC)TITLE_COL_SEEN=1; }
int ci_eq(char a,char b){ if(a>='A'&&a<='Z')a=(char)(a+32); if(b>='A'&&b<='Z')b=(char)(b+32); return a==b; }
#include "HTTP_FNS"
static int fails;
static void ok(int c,const char* m){ printf("%s %s\n",c?"ok":"FAIL",m); if(!c)fails++; }
static const char* dump(const char* s){ ON=0; OUT[0]=0; TITLE_COL_SEEN=0; http_text_dump(s,(int)strlen(s),1400); return OUT; }
int main(void){
    char hn[128]; u16 port; u32 ip;
    http_split_port("10.0.2.2:8088",hn,128,80,&port); ok(!strcmp(hn,"10.0.2.2")&&port==8088,"host:port splits into host and port");
    ok(parse_ip(hn,&ip)&&ip==0x0A000202u,"the split host is a dotted IPv4 and needs no DNS");
    http_split_port("example.com",hn,128,80,&port); ok(!strcmp(hn,"example.com")&&port==80,"no port keeps the default 80");
    http_split_port("example.com",hn,128,443,&port); ok(port==443,"no port keeps the default 443");
    http_split_port("host:0",hn,128,80,&port); ok(port==80,"port 0 is ignored");
    http_split_port("host:70000",hn,128,80,&port); ok(port==80,"port above 65535 is ignored");
    http_split_port("host:abc",hn,128,80,&port); ok(!strcmp(hn,"host")&&port==80,"a non-numeric port is ignored");
    const char* o=dump("<html><head><style>body{color:red}</style><title>My Page</title><script>var x=1;</script></head><body><h1>Hello</h1><p>one &amp; two</p></body></html>");
    ok(strstr(o,"color")==0,"the <style> block is skipped");
    ok(strstr(o,"var x")==0,"the <script> block is skipped");
    ok(strstr(o,"My Page\n\n")!=0,"the title ends with a blank line before the body");
    ok(TITLE_COL_SEEN,"the title is drawn in the accent colour");
    ok(strstr(o,"Hello\none & two")!=0,"block tags break lines and &amp; is decoded");
    o=dump("<p>a</p><!-- hidden <b>comment</b> --><p>b</p>"); ok(strstr(o,"comment")==0&&strstr(o,"a\nb")!=0,"comments are skipped");
    o=dump("<STYLE type=text/css>x{}</STYLE>text<SCRIPT src=a.js></SCRIPT>more"); ok(!strcmp(o,"textmore\n"),"upper-case style and script tags are skipped too");
    o=dump("<style>never closed"); ok(o[0]==0||!strcmp(o,"\n"),"an unterminated <style> does not read past the buffer");
    o=dump("a&nbsp;&nbsp;b &lt;tag&gt; &#39;q&#39;"); ok(!strcmp(o,"a b <tag> 'q'\n"),"entities decode and repeated spaces collapse");
    o=dump("<title>T</title><body><div>x</div><br>y<li>z</li></body>"); ok(strstr(o,"x\ny\nz")!=0,"div, br and li each start a new line");
    char big[5000]; int n=0; srand(3); for(int it=0;it<20000;it++){ n=rand()%300; for(int i=0;i<n;i++){ int r=rand()%6; big[i]=r==0?'<':r==1?'>':r==2?'&':r==3?'/':(char)('a'+rand()%26); } big[n]=0; dump(big); }
    ok(1,"20000 random tag soups did not crash");
    if(!fails)puts("HTTPTEXT all ok"); return fails!=0;
}
