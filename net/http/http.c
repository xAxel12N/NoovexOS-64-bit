#include "include/nvx/base.h"
#include "include/nvx/lib/crypto/nvxtls.h"
#include "include/nvx/lib/crypto/nvxx509.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/lib/crypto/nvxcrypto.h"
#include "include/nvx/drivers/rtc/cmos.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/drivers/storage/ahci.h"
#include "include/nvx/net/stack/tcpip.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/apps/internet/browser.h"
#include "include/nvx/apps/system/settings.h"
#include "include/nvx/net/http/cookies.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/graphics/theme/theme.h"
/* ---- HTTP client ---- */
u8 http_buf[262144];
   /* defined later */
static int g_ck_http=0;
static void ck_url(const char* host,char* url){ char* p=url; const char* h=g_ck_http?"http://":"https://"; while(*h)*p++=*h++; for(int i=0;host[i]&&i<180;i++)*p++=host[i]; *p++='/'; *p=0; }
static void ck_parse(const char* host,const u8* buf,int hdrend){
    char url[200]; ck_url(host,url);
    for(int i=0;i<hdrend;i++){ if(i!=0&&buf[i-1]!='\n')continue;
        const char* sc="set-cookie:"; int k=0; while(k<11 && i+k<hdrend && ci_eq((char)buf[i+k],sc[k]))k++;
        if(k!=11)continue; int j=i+11; while(j<hdrend&&(buf[j]==' '||buf[j]=='\t'))j++;
        char line[1024]; int q=0; while(j<hdrend&&buf[j]!='\r'&&buf[j]!='\n'&&q<1023)line[q++]=(char)buf[j++]; line[q]=0;
        cookie_set(url,line,0); }
}
static int ck_header(const char* host,char* out){
    char url[200]; ck_url(host,url); char tmp[380]; int n=cookie_header(url,0,tmp,368);
    if(n<=0){ out[0]=0; return 0; }
    int o=0; const char* p="Cookie: "; while(*p)out[o++]=*p++; for(int i=0;tmp[i];i++)out[o++]=tmp[i]; out[o++]='\r'; out[o++]='\n'; out[o]=0; return o;
}
int parse_ip(const char*s,u32*out){ u32 ip=0; int oct=0,val=0,any=0; for(;;s++){ if(*s>='0'&&*s<='9'){val=val*10+(*s-'0');any=1;} else if(*s=='.'||*s==0){ if(!any)return 0; ip=(ip<<8)|(u32)(val&0xFF); oct++; val=0; any=0; if(*s==0)break; } else return 0; } if(oct!=4)return 0; *out=ip; return 1; }
static int htd_tag_is(const char* t,const char* name){ int i=0; while(name[i]){ if(!ci_eq(t[i],name[i]))return 0; i++; } return !(t[i]>='a'&&t[i]<='z')&&!(t[i]>='A'&&t[i]<='Z')&&!(t[i]>='0'&&t[i]<='9'); }
static int htd_block(const char* t){ static const char* const B[]={"p","div","br","li","tr","h1","h2","h3","h4","h5","h6","ul","ol","table","section","article","header","footer","nav","blockquote","pre","hr","dt","dd","form","title",0}; for(int i=0;B[i];i++)if(htd_tag_is(t,B[i]))return 1; return 0; }
static int htd_skip_to(const char* b,int n,int i,const char* end){ int el=0; while(end[el])el++; for(;i<n;i++){ if(b[i]!='<'||i+1>=n||b[i+1]!='/')continue; int k=0; while(k<el&&i+2+k<n&&ci_eq(b[i+2+k],end[k]))k++; if(k==el){ while(i<n&&b[i]!='>')i++; return i+1; } } return n; }
static int htd_entity(const char* b,int n,int i,char* out){ static const char* const E[]={"amp;","&","lt;","<","gt;",">","quot;","\"","apos;","'","#39;","'","nbsp;"," ","#160;"," ","copy;","(c)","mdash;","-","ndash;","-","hellip;","...",0}; for(int k=0;E[k];k+=2){ int l=0; while(E[k][l]&&i+1+l<n&&b[i+1+l]==E[k][l])l++; if(!E[k][l]){ *out=E[k+1][0]; return 1+l; } } return 0; }
void http_text_dump(const char* b,int n,int limit){
    int shown=0,title=0; char last='\n';
    for(int i=0;i<n&&shown<limit;){ char c=b[i];
        if(c=='<'){
            if(i+3<n&&b[i+1]=='!'&&b[i+2]=='-'&&b[i+3]=='-'){ i+=4; while(i+2<n&&!(b[i]=='-'&&b[i+1]=='-'&&b[i+2]=='>'))i++; i+=3; continue; }
            const char* t=b+i+1; int close=0; if(*t=='/'){ close=1; t++; }
            if(!close&&htd_tag_is(t,"style")){ i=htd_skip_to(b,n,i+1,"style"); continue; }
            if(!close&&htd_tag_is(t,"script")){ i=htd_skip_to(b,n,i+1,"script"); continue; }
            if(!close&&htd_tag_is(t,"head")){ int j=i+1; int ti=-1; for(;j<n;j++){ if(b[j]=='<'&&!(j+1<n&&b[j+1]=='/')&&htd_tag_is(b+j+1,"title")){ ti=j; break; } if(b[j]=='<'&&j+1<n&&b[j+1]=='/'&&htd_tag_is(b+j+2,"head"))break; } if(ti<0){ i=htd_skip_to(b,n,i+1,"head"); if(i>=n)i=n; continue; } }
            if(!close&&htd_tag_is(t,"title")){ title=1; tcol(ACC); }
            if(close&&htd_tag_is(t,"title")){ title=0; tcol(GREY); term_putc('\n'); term_putc('\n'); last='\n'; }
            int blk=htd_block(t);
            while(i<n&&b[i]!='>')i++; i++;
            if(blk&&!title&&last!='\n'){ term_putc('\n'); last='\n'; }
            continue; }
        if(c=='&'){ char e; int l=htd_entity(b,n,i,&e); if(l){ c=e; i+=l; if(c==' '&&(last==' '||last=='\n')){ continue; } term_putc(c); last=c; shown++; continue; } }
        i++;
        if(c=='\r')continue;
        if(c==' '||c=='\n'||c=='\t'){ if(last==' '||last=='\n')continue; c=' '; }
        if((unsigned char)c<32)continue;
        term_putc(c); last=c; shown++; }
    if(title)tcol(GREY);
    if(last!='\n')term_putc('\n');
}
int http_split_port(const char* host0,char* hn,int cap,u16 def,u16* port){
    int hi=0; for(;host0[hi]&&host0[hi]!=':'&&hi<cap-1;hi++)hn[hi]=host0[hi]; hn[hi]=0; *port=def;
    if(host0[hi]!=':')return 0;
    u32 pv=0; int j=hi+1; for(;host0[j]>='0'&&host0[j]<='9'&&pv<65536;j++)pv=pv*10+(u32)(host0[j]-'0');
    if(pv>0&&pv<65536)*port=(u16)pv; return 1;
}
static void (*g_hp_saved)(void)=0; static int g_hp_on=0; static u64 g_hp_t=0;
int g_http_cmd_busy=0;
static void http_pump(void){ extern void busy_pump(void); extern void desk_nested_step(void); busy_pump(); if(g_ticks-g_hp_t>=3){ g_hp_t=g_ticks; desk_nested_step(); } }
void http_pump_begin(void){ if(g_hp_on)return; g_hp_on=1; g_http_cmd_busy=1; g_hp_saved=g_wait_pump; g_hp_t=g_ticks; g_wait_pump=http_pump; }
void http_pump_end(void){ if(!g_hp_on)return; g_wait_pump=g_hp_saved; g_hp_on=0; g_http_cmd_busy=0; }
int http_fetch_to(const char*host0,const char*path,u8*buf,int cap){
    char hn[128]; u16 port; http_split_port(host0,hn,(int)sizeof hn,80,&port); const char* host=hn;
    u32 ip; if(parse_ip(host,&ip)){} else { int r=dns_query(host,&ip); if(r<=0) return -100; }
    char req[960]; int r=0; const char* g="GET "; while(*g)req[r++]=*g++;
    for(int i=0;path[i]&&r<380;i++)req[r++]=path[i];
    const char* h1=" HTTP/1.0\r\nHost: "; for(int i=0;h1[i]&&r<440;i++)req[r++]=h1[i];
    for(int i=0;host0[i]&&r<500;i++)req[r++]=host0[i];
    const char* h2="\r\nConnection: close\r\nUser-Agent: Noovex64\r\n"; for(int i=0;h2[i]&&r<560;i++)req[r++]=h2[i];
    { char ch[400]; g_ck_http=1; ck_header(host,ch); g_ck_http=0; for(int i=0;ch[i]&&r<940;i++)req[r++]=ch[i]; }
    req[r++]='\r'; req[r++]='\n';
    int n=tcp_get(ip,port,req,r,buf,cap-1);
    if(n>0){ int he=n; for(int i=0;i+3<n;i++)if(buf[i]=='\r'&&buf[i+1]=='\n'&&buf[i+2]=='\r'&&buf[i+3]=='\n'){he=i+2;break;} g_ck_http=1; ck_parse(host,buf,he); g_ck_http=0; }
    return n;
}
int http_fetch(const char*host,const char*path){ return http_fetch_to(host,path,http_buf,sizeof(http_buf)); }

/* ---- entropy (rdtsc + CMOS + RDRAND, mixed through SHA-256) ---- */
static u64 rdtsc(void){ u32 a,d; { u64 t_=cpu_tsc(); a=(u32)t_; d=(u32)(t_>>32); }; return ((u64)d<<32)|a; }
static int cpu_rdrand(void){ u32 a,b,c,d; cpu_cpuid(1u,0u,&a,&b,&c,&d); (void)a;(void)b;(void)d; return (c>>30)&1; }
static int rdrand32(u32*x){ unsigned char ok=0; ok=(unsigned char)cpu_rand32(x); return ok; }
static u8 ENT_POOL[32]; static int ent_ready=0; static u32 ent_ctr=0;
static void entropy_init(void){ u8 s[80]; int p=0; u64 t=rdtsc(); for(int i=0;i<8;i++)s[p++]=(u8)(t>>(i*8));
    for(int i=0;i<10;i++)s[p++]=cmos((u8)i);
    if(cpu_rdrand()){ for(int i=0;i<6;i++){ u32 r; if(rdrand32(&r)){ s[p++]=(u8)r;s[p++]=(u8)(r>>8);s[p++]=(u8)(r>>16);s[p++]=(u8)(r>>24);} } }
    for(int i=0;i<8&&p<72;i++){ for(volatile int d=0;d<1500;d++); u64 t2=rdtsc(); s[p++]=(u8)t2; }
    sha256_hash(s,p,ENT_POOL); ent_ready=1;
}
void entropy_get(u8* out,int n){ if(!ent_ready)entropy_init(); int o=0;
    while(o<n){ u8 b[44]; for(int i=0;i<32;i++)b[i]=ENT_POOL[i]; u32 c=ent_ctr++; b[32]=(u8)c;b[33]=(u8)(c>>8);b[34]=(u8)(c>>16);b[35]=(u8)(c>>24);
        u64 t=rdtsc(); for(int i=0;i<8;i++)b[36+i]=(u8)(t>>(i*8)); u8 blk[32]; sha256_hash(b,44,blk);
        for(int i=0;i<32&&o<n;i++)out[o++]=blk[i];
        u8 rb[33]; for(int i=0;i<32;i++)rb[i]=ENT_POOL[i]; rb[32]=1; sha256_hash(rb,33,ENT_POOL); }
}
void secure_zero(void*p,int n){ volatile u8*v=(volatile u8*)p; for(int i=0;i<n;i++)v[i]=0; }

/* ---- streaming TCP (for TLS) ---- */
static u32 ts_dip; static u16 ts_dport, ts_sp; static u8 ts_dmac[6];
static u32 ts_snd, ts_rcv; static int ts_est, ts_fin;
int tcps_open(u32 dip,u16 dport){
    if(!nic_up()||!my_ip)return 0;
    u32 route=((dip&my_mask)==(my_ip&my_mask))?dip:gw_ip;
    if(!arp_resolve(route,ts_dmac))return 0;
    nic_kick(); nic_flush_rx();
    ts_dip=dip; ts_dport=dport; ts_sp=next_ephport();
    ts_snd=net_rand(); ts_rcv=0; ts_est=0; ts_fin=0; u8 rx[1600];
    for(int att=0;att<4&&!ts_est;att++){ tcp_send(ts_dip,ts_sp,ts_dport,ts_dmac,ts_snd,0,TF_SYN,0,0);
        for(int k=0;k<300;k++){ int n=net_poll(rx,1600,1); if(n<54)continue; if(rd16(rx+12)!=0x0800)continue; u8*ip=rx+14; if(ip[9]!=6){ dhcp_async_take(rx,n); continue; } int ihl=(ip[0]&0x0F)*4; u8*t=ip+ihl; if(rd16(t+0)!=ts_dport||rd16(t+2)!=ts_sp)continue; u8 fl=t[13];
            if((fl&(TF_SYN|TF_ACK))==(TF_SYN|TF_ACK)&&rd32(t+8)==ts_snd+1){ ts_rcv=rd32(t+4)+1; ts_snd+=1; ts_est=1; break; }
            if(fl&TF_RST)return 0; } }
    if(!ts_est){ netlog("TLS TCP no SYN-ACK"); return 0; }
    tcp_send(ts_dip,ts_sp,ts_dport,ts_dmac,ts_snd,ts_rcv,TF_ACK,0,0); netlog("TLS TCP established"); return 1;
}
int tcps_wr(void* c,const u8* buf,int len){ (void)c; int off=0;
    while(off<len){ int chunk=len-off; if(chunk>1400)chunk=1400; tcp_send(ts_dip,ts_sp,ts_dport,ts_dmac,ts_snd,ts_rcv,TF_PSH|TF_ACK,buf+off,chunk); ts_snd+=(u32)chunk; off+=chunk; } return len;
}
int tcps_rd(void* c,u8* buf,int max){ (void)c; if(ts_fin)return 0; u8 rx[1600];
    for(int idle=0; idle<2500; ){ int n=net_poll(rx,1600,1); if(n<=0){ idle++; continue; }
        if(n<54||rd16(rx+12)!=0x0800)continue; u8*ip=rx+14; if(ip[9]!=6){ dhcp_async_take(rx,n); continue; } int ihl=(ip[0]&0x0F)*4; if(ihl<20||14+ihl+20>n)continue; u8*t=ip+ihl; if(rd16(t+0)!=ts_dport||rd16(t+2)!=ts_sp)continue;
        int iptot=rd16(ip+2); int thl=((t[12]>>4)&0xF)*4; if(thl<20||14+ihl+thl>n)continue; int dl=iptot-ihl-thl; { int av=n-14-ihl-thl; if(dl>av)dl=av; } if(dl<0)dl=0; u32 seq=rd32(t+4); u8 fl=t[13];
        if(fl&TF_RST){ ts_fin=1; return 0; }
        if(dl>0){ if(seq==ts_rcv){ int cp=dl; if(cp>max)cp=max; for(int i=0;i<cp;i++)buf[i]=t[thl+i]; ts_rcv+=(u32)cp;
                tcp_send(ts_dip,ts_sp,ts_dport,ts_dmac,ts_snd,ts_rcv,TF_ACK,0,0);
                if(cp==dl&&(fl&TF_FIN)){ ts_rcv+=1; ts_fin=1; tcp_send(ts_dip,ts_sp,ts_dport,ts_dmac,ts_snd,ts_rcv,TF_FIN|TF_ACK,0,0); ts_snd+=1; } return cp; }
            else { tcp_send(ts_dip,ts_sp,ts_dport,ts_dmac,ts_snd,ts_rcv,TF_ACK,0,0); } }
        else if(fl&TF_FIN){ if(seq!=ts_rcv){ tcp_send(ts_dip,ts_sp,ts_dport,ts_dmac,ts_snd,ts_rcv,TF_ACK,0,0); continue; } ts_rcv+=1; ts_fin=1; tcp_send(ts_dip,ts_sp,ts_dport,ts_dmac,ts_snd,ts_rcv,TF_FIN|TF_ACK,0,0); ts_snd+=1; return 0; } }
    return 0;
}
 
static char g_tls_ca_pem[24576]; static u8 g_tls_ca_der[X509_MAX_EXTRA][2048];
void tls_prepare(void){
    g_x509_nextra=0;
    int n=ux_read_file("/etc/ssl/local-ca.pem",g_tls_ca_pem,(int)sizeof g_tls_ca_pem); if(n<=0)return;
    const char* p=g_tls_ca_pem;
    for(int i=0;i<X509_MAX_EXTRA;i++){ int l=x509_pem_next(&p,g_tls_ca_der[i],2048); if(l<=0)break; if(x509_add_anchor_der(g_tls_ca_der[i],l))netlog("TLS: skipped an unreadable certificate in /etc/ssl/local-ca.pem"); }
    if(g_x509_nextra)netlog("TLS: using extra roots from /etc/ssl/local-ca.pem");
}
i64 tls_clock_now(void){ i64 s=(i64)(k_realtime_ms()/1000ULL); return s>1704067200LL?s:0; }
int https_fetch_to(const char*host0,const char*path,u8*buf,int cap){
    char hn[128]; u16 port; http_split_port(host0,hn,(int)sizeof hn,443,&port); const char* host=hn;
    u32 ip; if(parse_ip(host,&ip)){} else { int r=dns_query(host,&ip); if(r<=0){ netlog("HTTPS: DNS lookup failed"); return -100; } }
    if(!tcps_open(ip,port)){ netlog("HTTPS: TCP connect failed"); return -2; }
    char req[960]; int r=0; const char* g="GET "; while(*g)req[r++]=*g++;
    for(int i=0;path[i]&&r<380;i++)req[r++]=path[i];
    const char* h1=" HTTP/1.0\r\nHost: "; for(int i=0;h1[i]&&r<440;i++)req[r++]=h1[i];
    for(int i=0;host0[i]&&r<500;i++)req[r++]=host0[i];
    const char* h2="\r\nConnection: close\r\nUser-Agent: Noovex64\r\n"; for(int i=0;h2[i]&&r<560;i++)req[r++]=h2[i];
    { char ch[400]; ck_header(host,ch); for(int i=0;ch[i]&&r<940;i++)req[r++]=ch[i]; }
    req[r++]='\r'; req[r++]='\n';
    int hl=0; while(host[hl])hl++;
    u8 ent[64]; entropy_get(ent,64);
    tls_io* io=&g_tls_io; io->wr=tcps_wr; io->rd=tcps_rd; io->ctx=0; io->rlen=0; io->rpos=0;
    netlog("TLS handshake start");
    int n=tls_get(io,host,hl,ent,req,r,buf,cap-1);
    secure_zero(ent,64);
    if(n>0){ int he=n; for(int i=0;i+3<n;i++)if(buf[i]=='\r'&&buf[i+1]=='\n'&&buf[i+2]=='\r'&&buf[i+3]=='\n'){he=i+2;break;} ck_parse(host,buf,he); }
    { char b[120],*p=b; p=scopy(p,"TLS result "); if(n<0){*p++='-'; p=udec(p,(u32)(-n));}else p=udec(p,(u32)n); p=scopy(p,": "); p=scopy(p,g_tls_why); *p=0; netlog(b); klog(b); }
    return n;
}
int https_fetch(const char*host,const char*path){ return https_fetch_to(host,path,http_buf,sizeof(http_buf)); }

/* ---- generic HTTPS request: method + extra headers + optional body (for OAuth) ---- */
static u8 g_httpreq[1600];
int https_request(const char*host,const char*path,const char*method,const char*xhdr,const char*body,int blen,u8*buf,int cap){
    u32 ip; if(parse_ip(host,&ip)){} else { if(dns_query(host,&ip)<=0) return -100; }
    if(!tcps_open(ip,443)) return -2;
    char* req=(char*)g_httpreq; int r=0;
    for(const char* m=method;*m&&r<16;m++)req[r++]=*m; req[r++]=' ';
    for(int i=0;path[i]&&r<640;i++)req[r++]=path[i];
    const char* h1=" HTTP/1.0\r\nHost: "; for(int i=0;h1[i];i++)req[r++]=h1[i];
    for(int i=0;host[i]&&r<920;i++)req[r++]=host[i];
    const char* h2="\r\nConnection: close\r\nUser-Agent: Noovex64\r\nAccept: application/json\r\n"; for(int i=0;h2[i];i++)req[r++]=h2[i];
    if(xhdr)for(int i=0;xhdr[i]&&r<1100;i++)req[r++]=xhdr[i];
    if(blen>0){ const char* cl="Content-Length: "; for(int i=0;cl[i];i++)req[r++]=cl[i];
        char t[12];int tn=0;int b=blen;if(b==0)t[tn++]='0';while(b){t[tn++]=(char)('0'+b%10);b/=10;}while(tn)req[r++]=t[--tn]; req[r++]='\r';req[r++]='\n'; }
    req[r++]='\r'; req[r++]='\n';
    if(blen>0)for(int i=0;i<blen && r<1590;i++)req[r++]=body[i];
    int hl=0; while(host[hl])hl++;
    u8 ent[64]; entropy_get(ent,64);
    tls_io* io=&g_tls_io; io->wr=tcps_wr; io->rd=tcps_rd; io->ctx=0; io->rlen=0; io->rpos=0;
    int n=tls_get(io,host,hl,ent,req,r,buf,cap-1);
    secure_zero(ent,64);
    if(n<0){ char b[120],*p=b; p=scopy(p,"TLS: "); p=scopy(p,g_tls_why); *p=0; netlog(b); }
    return n;
}
/* extract a JSON string field:  "key":"value"  ->  value.  returns len or -1 */
int json_str(const u8* j,int n,const char* key,char* out,int cap){
    int kl=0; while(key[kl])kl++;
    for(int i=0;i+kl+3<n;i++){
        if(j[i]=='"'){ int m=1; for(int q=0;q<kl;q++)if(j[i+1+q]!=(u8)key[q]){m=0;break;}
            if(m && j[i+1+kl]=='"'){ int p=i+1+kl+1; while(p<n && j[p]!=':')p++; p++;
                while(p<n && (j[p]==' '||j[p]=='\t'||j[p]=='\n'||j[p]=='\r'))p++;
                if(p<n && j[p]=='"'){ p++; int o=0; while(p<n && j[p]!='"' && o<cap-1){ if(j[p]=='\\'&&p+1<n){ p++; char c=(char)j[p]; out[o++]=(c=='n')?'\n':(c=='t')?'\t':c; } else out[o++]=(char)j[p]; p++; } out[o]=0; return o; }
                return -1; } } }
    return -1;
}
int nvx_json_int(const u8* j,int n,const char* key,int def){
    int kl=0; while(key[kl])kl++;
    for(int i=0;i+kl+2<n;i++){ if(j[i]=='"'){ int m=1; for(int q=0;q<kl;q++)if(j[i+1+q]!=(u8)key[q]){m=0;break;}
        if(m&&j[i+1+kl]=='"'){ int p=i+kl+2; while(p<n&&j[p]!=':')p++; p++; while(p<n&&(j[p]==' '||j[p]=='"'))p++;
            int neg=0; if(p<n&&j[p]=='-'){neg=1;p++;} if(p>=n||j[p]<'0'||j[p]>'9')return def;
            int v=0; while(p<n&&j[p]>='0'&&j[p]<='9'){v=v*10+(j[p]-'0');p++;} return neg?-v:v; } } }
    return def;
}
/* ---- Noovex OAuth (noovexos.site) - public client_id embedded by choice of the OS owner ---- */

char g_nvx_token[320]="", g_nvx_user[64]="", g_nvx_email[96]="", g_nvx_name[64]="", g_nvx_state[10]="";
int  g_nvx_signedin=0;
char su_msg[96]="";
int  g_cloud_fs=0, g_fs_pulling=0;                    /* wizard choice: cloud vs local files */
    /* defined after build_bg */
    /* defined after scratch buffers */
   /* defined after browser_go */
static int nvx_userinfo_from_token(const char* tok){
    char hdr[360],*hp=hdr; hp=scopy(hp,"Authorization: Bearer ");hp=scopy(hp,tok);hp=scopy(hp,"\r\n");*hp=0;
    int m=https_request(NVX_HOST,"/api/public/oauth/userinfo","GET",hdr,0,0,http_buf,(int)sizeof(http_buf));
    if(m<=0)return -1; int he=0; for(int i=0;i+3<m;i++)if(http_buf[i]=='\r'&&http_buf[i+1]=='\n'&&http_buf[i+2]=='\r'&&http_buf[i+3]=='\n'){he=i+4;break;}
    g_nvx_user[0]=g_nvx_email[0]=g_nvx_name[0]=0;
    json_str(http_buf+he,m-he,"username",g_nvx_user,(int)sizeof(g_nvx_user));
    json_str(http_buf+he,m-he,"name",g_nvx_name,(int)sizeof(g_nvx_name));
    g_nvx_signedin=1; nvx_settings_load(); nvx_files_pull();   /* pull saved settings + cloud files */
    return 0;
}
static char* nvx_jesc(char* p,const char* s){
    static const char hx[]="0123456789abcdef";
    for(int i=0;s[i]&&i<64;i++){ u8 c=(u8)s[i];
        if(c=='"'||c=='\\'){ *p++='\\'; *p++=(char)c; }
        else if(c<0x20||c==0x7F){ *p++='\\'; *p++='u'; *p++='0'; *p++='0'; *p++=hx[c>>4]; *p++=hx[c&15]; }
        else *p++=(char)c; }
    return p;
}
/* direct sign-in via the OAuth password grant (no browser) */
int nvx_signin(const char* email,const char* pwd){
    char body[1024],*p=body; p=scopy(p,"{\"grant_type\":\"password\",\"username\":\"");p=nvx_jesc(p,email);
    p=scopy(p,"\",\"password\":\"");p=nvx_jesc(p,pwd);p=scopy(p,"\",\"client_id\":\"" NVX_CID "\",\"client_secret\":\"" NVX_SECRET "\"}");*p=0;
    int n=https_request(NVX_HOST,"/api/public/oauth/token","POST","Content-Type: application/json\r\n",body,(int)(p-body),http_buf,(int)sizeof(http_buf));
    if(n<=0){ scopy(su_msg,"no network / TLS error"); return -1; }
    int he=0; for(int i=0;i+3<n;i++)if(http_buf[i]=='\r'&&http_buf[i+1]=='\n'&&http_buf[i+2]=='\r'&&http_buf[i+3]=='\n'){he=i+4;break;}
    char tok[320]; if(json_str(http_buf+he,n-he,"access_token",tok,(int)sizeof(tok))<=0){ su_msg[0]=0; if(json_str(http_buf+he,n-he,"error_description",su_msg,(int)sizeof(su_msg))<=0) json_str(http_buf+he,n-he,"error",su_msg,(int)sizeof(su_msg)); if(!su_msg[0])scopy(su_msg,"sign-in failed"); return -2; }
    scopy(g_nvx_token,tok); nvx_userinfo_from_token(tok); g_nvx_signedin=1; return 0;
}
/* create a new Noovex account, then sign in */
int nvx_register(const char* uname,const char* pwd){
    char body[1024],*p=body; p=scopy(p,"{\"username\":\"");p=nvx_jesc(p,uname);
    p=scopy(p,"\",\"password\":\"");p=nvx_jesc(p,pwd);p=scopy(p,"\",\"client_id\":\"" NVX_CID "\"}");*p=0;
    int n=https_request(NVX_HOST,"/api/public/register","POST","Content-Type: application/json\r\n",body,(int)(p-body),http_buf,(int)sizeof(http_buf));
    if(n<=0){ scopy(su_msg,"no network / TLS error"); return -1; }
    int he=0; for(int i=0;i+3<n;i++)if(http_buf[i]=='\r'&&http_buf[i+1]=='\n'&&http_buf[i+2]=='\r'&&http_buf[i+3]=='\n'){he=i+4;break;}
    char tok[320];
    if(json_str(http_buf+he,n-he,"access_token",tok,(int)sizeof(tok))>0){ scopy(g_nvx_token,tok); nvx_userinfo_from_token(tok); g_nvx_signedin=1; return 0; }
    su_msg[0]=0; if(json_str(http_buf+he,n-he,"error_description",su_msg,(int)sizeof(su_msg))>0 || json_str(http_buf+he,n-he,"error",su_msg,(int)sizeof(su_msg))>0) return -2;
    return nvx_signin(uname,pwd);   /* registered but no token returned -> log in */
}
/* push a visited URL to the signed-in account's history (fire-and-forget) */
void nvx_hist_add(const char* url){
    if(!g_nvx_signedin || !url || !url[0]) return;
    char body[340],*p=body; p=scopy(p,"{\"url\":\"");
    for(const char* u=url; *u && (p-body)<312; u++){ if(*u=='"'||*u=='\\')*p++='\\'; *p++=*u; }
    p=scopy(p,"\"}"); *p=0;
    char hdr[380],*hp=hdr; hp=scopy(hp,"Content-Type: application/json\r\nAuthorization: Bearer ");hp=scopy(hp,g_nvx_token);hp=scopy(hp,"\r\n");*hp=0;
    https_request(NVX_HOST,"/api/public/history","POST",hdr,body,(int)(p-body),http_buf,(int)sizeof(http_buf));
}

/* =========================================================================
   Input state (filled by IRQ handlers)
   ========================================================================= */
volatile u64 g_ticks=0;                    /* PIT ticks (100 Hz)     */
volatile int mouse_dx=0, mouse_dy=0;        /* accumulated deltas     */
volatile int mouse_btn=0, mouse_btn_prev=0; volatile int mouse_lpress=0, mouse_rpress=0, mouse_mpress=0; volatile u64 mouse_press_t=0; /* left button            */
volatile int mouse_rbtn=0, mouse_rbtn_prev=0;/* right button           */
volatile int mouse_scroll=0;                /* wheel delta            */
volatile int mouse_mid=0;                   /* middle button (ported driver) */
int g_mbytes=3;                             /* 3, or 4 = IntelliMouse */
volatile int mouse_evt=0;

#define KBUFN 64
static volatile char kbuf[KBUFN]; volatile int kh=0,kt=0;
extern int kshift; extern volatile unsigned char kdown[128]; static unsigned char kmod[KBUFN]; int g_key_ctrl=0, g_key_shift=0, g_key_rep=0; extern int g_krep_flag;
void kpush(char c){ int n=(kh+1)%KBUFN; if(n!=kt){kbuf[kh]=c; kmod[kh]=(unsigned char)((kdown[0x1D]?1:0)|(kshift?2:0)|(g_krep_flag?4:0)); kh=n;} }
volatile unsigned char kdown[128];
int kpending(void){ return kh!=kt; }
void kidle(void){ extern int xhid_n; extern void xhci_mouse_poll(void); extern volatile u32 g_ml_beat, g_nest_beat, g_nest_live; extern int term_prompt_visible(void); g_ml_beat++; if(term_prompt_visible())g_nest_live++; else g_nest_beat++; if(xhid_n)xhci_mouse_poll(); if(kh==kt)cpu_idle(); }
int kpop(void){ if(kh==kt)return -1; int c=(unsigned char)kbuf[kt]; g_key_ctrl=kmod[kt]&1; g_key_shift=(kmod[kt]>>1)&1; g_key_rep=(kmod[kt]>>2)&1; kt=(kt+1)%KBUFN; return c; }
