#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "drivers/usb/class/ippusb_proto.h"
static int fails=0, oks=0;
static void ck(int c,const char* m){ if(c){ oks++; printf("%s ok\n",m); } else { fails++; printf("%s FAIL\n",m); } }
static const unsigned char CFG[]={9,2,62,0,2,1,0,0xC0,1, 9,4,0,0,2,7,1,2,0, 7,5,0x01,2,0,2,0, 7,5,0x81,2,0,2,0, 9,4,1,0,2,7,1,4,0, 7,5,0x02,2,0,2,0, 7,5,0x82,2,0,2,0};
static unsigned char sim_ipp[2048]; static int sim_ippn;
static void ipp_attr(int tag,const char* n,const char* v){ unsigned char* e=sim_ipp+sizeof sim_ipp; unsigned char* p=upp_attr(sim_ipp+sim_ippn,e,tag,n,v); sim_ippn=(int)(p-sim_ipp); }
static int sim_printer(const unsigned char* req,int n,unsigned char* out,int chunked){
    if(n<4||memcmp(req,"POST",4))return sprintf((char*)out,"HTTP/1.1 400 Bad\r\nContent-Length: 0\r\n\r\n");
    const unsigned char* b=(const unsigned char*)strstr((const char*)req,"\r\n\r\n"); if(!b)return 0; b+=4;
    int op=(b[2]<<8)|b[3]; sim_ippn=0; unsigned char hd[8]={2,0,0,0,b[4],b[5],b[6],b[7]}; memcpy(sim_ipp,hd,8); sim_ippn=8; sim_ipp[sim_ippn++]=1;
    ipp_attr(0x47,"attributes-charset","utf-8"); sim_ipp[sim_ippn++]=4;
    if(op==0x0B){ ipp_attr(0x41,"printer-make-and-model","SimJet 3000"); ipp_attr(0x49,"document-format-supported","application/octet-stream"); ipp_attr(0x49,"","image/pwg-raster"); ipp_attr(0x49,"","image/urf"); ipp_attr(0x49,"","application/pdf");
        unsigned char st[4]={0,0,0,3}; sim_ipp[sim_ippn++]=0x23; sim_ipp[sim_ippn++]=0; sim_ipp[sim_ippn++]=13; memcpy(sim_ipp+sim_ippn,"printer-state",13); sim_ippn+=13; sim_ipp[sim_ippn++]=0; sim_ipp[sim_ippn++]=4; memcpy(sim_ipp+sim_ippn,st,4); sim_ippn+=4;
        ipp_attr(0x44,"printer-state-reasons","none"); }
    sim_ipp[sim_ippn++]=3;
    int o=0;
    if(!chunked){ o=sprintf((char*)out,"HTTP/1.1 200 OK\r\nContent-Type: application/ipp\r\nContent-Length: %d\r\n\r\n",sim_ippn); memcpy(out+o,sim_ipp,sim_ippn); return o+sim_ippn; }
    o=sprintf((char*)out,"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"); int i=0; while(i<sim_ippn){ int c=sim_ippn-i>7?7:sim_ippn-i; o+=sprintf((char*)out+o,"%x\r\n",c); memcpy(out+o,sim_ipp+i,c); o+=c; out[o++]='\r'; out[o++]='\n'; i+=c; }
    o+=sprintf((char*)out+o,"0\r\n\r\n"); return o; }
int main(void){
    struct uprn_if f[4]; int n=upp_parse_cfg(CFG,sizeof CFG,f,4);
    ck(n==2,"config: two printer interfaces"); ck(f[0].proto==2&&f[0].oe==1&&f[0].ie==0x81&&f[0].om==512,"config: bidirectional bulk pair");
    ck(upp_pick(f,n,1)==1&&f[1].proto==4,"config: IPP-over-USB interface picked"); ck(upp_pick(f,n,0)==0,"config: legacy interface picked for raw");
    const unsigned char bad[]={9,4,0,0,1,7,1,1,0, 255,5,1};
    ck(upp_parse_cfg(bad,sizeof bad,f,4)==0,"config: truncated descriptor rejected");
    const char* id="MFG:Sim Corp;CMD:PJL,PCL,PWGRaster,URF;MDL:SimJet 3000;CLS:PRINTER;";
    char v[64]; ck(upp_devid(id,"MDL",v,64)&&!strcmp(v,"SimJet 3000"),"device id: model"); ck(upp_devid(id,"CMD",v,64)&&!strcmp(v,"PJL,PCL,PWGRaster,URF"),"device id: command set"); ck(!upp_devid(id,"DES",v,64),"device id: missing key");
    unsigned char raw[8]={0,10,'M','D','L',':','X',';'}; ck(upp_devid_len(raw,8)==-1||upp_devid_len(raw,8)==0||upp_devid_len(raw,8)<=6,"device id: length bounded");
    unsigned char req[1024], body[512]; int bl=upp_ipp_getattrs(body,sizeof body,"ipp://localhost/ipp/print",7); ck(bl>40&&body[3]==0x0B&&body[7]==7,"ipp: Get-Printer-Attributes encoded");
    int hl=sprintf((char*)req,"POST /ipp/print HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/ipp\r\nContent-Length: %d\r\n\r\n",bl); memcpy(req+hl,body,bl);
    for(int ch=0;ch<2;ch++){ static unsigned char resp[8192]; int rn=sim_printer(req,hl+bl,resp,ch);
        static unsigned char acc[8192]; int an=0, done=0; struct uph h; memset(&h,0,sizeof h);
        for(int i=0;i<rn&&!done;){ int c=rn-i>13?13:rn-i; memcpy(acc+an,resp+i,c); an+=c; i+=c; int r=upp_http(acc,an,&h); if(r<0)break; if(r>0)done=1; }
        ck(done&&h.status==200,ch?"http: chunked response reassembled in 13-byte bulk reads":"http: content-length response in 13-byte bulk reads");
        struct uppinfo pi; upp_ipp_parse(acc+h.hlen,h.blen,&pi);
        ck(pi.status==0&&!strcmp(pi.model,"SimJet 3000"),"ipp: status and make-and-model"); ck(pi.pwg&&pi.urf&&pi.pdf&&!pi.jpeg,"ipp: document formats"); ck(pi.state==3&&!strcmp(pi.reasons,"none"),"ipp: printer state"); }
    unsigned char part[]="HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc"; struct uph h; ck(upp_http(part,(int)sizeof part-1,&h)==0,"http: incomplete body waits");
    unsigned char junk[]="garbage\r\n\r\n"; ck(upp_http(junk,(int)sizeof junk-1,&h)<0,"http: non-HTTP rejected");
    unsigned char badc[]="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n"; ck(upp_http(badc,(int)sizeof badc-1,&h)<0,"http: bad chunk size rejected");
    unsigned char loc[]="HTTP/1.1 201 Created\r\nLocation: http://localhost/eSCL/ScanJobs/42\r\nContent-Length: 0\r\n\r\n"; char L[128], P[128];
    ck(upp_http(loc,(int)sizeof loc-1,&h)==1&&h.status==201&&upp_hdr(loc,h.hlen,"location",L,128)&&upp_path(L,P,128)&&!strcmp(P,"/eSCL/ScanJobs/42"),"escl: job Location path");
    char job[2048]; int jl=upp_escl_job(job,sizeof job,300,1,"image/jpeg",210,297); ck(jl>0&&strstr(job,"<scan:XResolution>300<")&&strstr(job,"RGB24")&&strstr(job,"<pwg:Width>2480<"),"escl: ScanSettings XML");
    const char* caps="<scan:ScannerCapabilities><pwg:Version>2.63</pwg:Version><pwg:MakeAndModel>SimJet 3000</pwg:MakeAndModel><scan:Platen/><scan:DiscreteResolution><scan:XResolution>75</scan:XResolution></scan:DiscreteResolution><scan:DiscreteResolution><scan:XResolution>600</scan:XResolution></scan:DiscreteResolution></scan:ScannerCapabilities>";
    ck(upp_xml(caps,(int)strlen(caps),"MakeAndModel",0,v,64)&&!strcmp(v,"SimJet 3000"),"escl: capabilities model"); ck(upp_xml(caps,(int)strlen(caps),"XResolution",1,v,64)&&!strcmp(v,"600"),"escl: nth resolution");
    srand(7); for(int t=0;t<20000;t++){ unsigned char b[256]; int m=rand()%256; for(int i=0;i<m;i++)b[i]=(unsigned char)rand(); if(t&1)memcpy(b,"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n",m<48?m:48); struct uph q; upp_http(b,m,&q); struct uppinfo pi; upp_ipp_parse(b,m,&pi); upp_parse_cfg(b,m,f,4); char o[64]; b[m?m-1:0]=0; upp_xml((char*)b,m,"X",0,o,64); }
    ck(1,"fuzz: 20000 random buffers without a crash");
    printf("%d ok, %d failed\n%s\n",oks,fails,fails?"FAILED":"ALL PASS"); return fails!=0; }
