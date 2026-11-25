#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/mem/heap.h"
#include "drivers/usb/class/ippusb_proto.h"
int uprn_scan(void); int uprn_has_ipp(void); int uprn_http(const u8* req,int len,u8* resp,int cap,int* blen,int* boff);
int pic_store(const char* name,const u8* d,long n);
char g_scan_status[96]="No scan yet"; char g_scan_model[64]; char g_scan_last[80];
static int escl_req(const char* method,const char* path,const char* body,int bl,u8* resp,int cap,int* rl,int* ro){
    static u8 rq[4096]; char* h=(char*)rq; h=scopy(h,method); *h++=' '; h=scopy(h,path); h=scopy(h," HTTP/1.1\r\nHost: localhost\r\n");
    if(body){ h=scopy(h,"Content-Type: text/xml\r\nContent-Length: "); h=udec(h,(u32)bl); h=scopy(h,"\r\n"); }
    h=scopy(h,"\r\n"); int hl=(int)(h-(char*)rq); if(hl+bl>(int)sizeof rq)return -1; for(int i=0;i<bl;i++)rq[hl+i]=(u8)body[i];
    return uprn_http(rq,hl+bl,resp,cap,rl,ro); }
static int escl_caps(void){
    static u8 rb[16384]; int rl=0, ro=0; int http=escl_req("GET","/eSCL/ScannerCapabilities",0,0,rb,(int)sizeof rb-1,&rl,&ro);
    if(http!=200)return http<0?http:-http;
    rb[ro+rl]=0; g_scan_model[0]=0; upp_xml((const char*)rb+ro,rl,"MakeAndModel",0,g_scan_model,64); return 0; }
int escl_scan(int dpi,int color){
    char* p=g_scan_status;
    if(!uprn_scan()||!uprn_has_ipp()){ scopy(p,"No USB scanner with IPP over USB found")[0]=0; return -1; }
    int r=escl_caps(); if(r){ p=scopy(p,"The device does not offer eSCL scanning ("); p=udec(p,(u32)(r<0?-r:r)); p=scopy(p,")"); *p=0; return -2; }
    if(dpi<75)dpi=75; if(dpi>600)dpi=600;
    static char job[2048]; int jl=upp_escl_job(job,(int)sizeof job,dpi,color,"image/jpeg",210,297); if(jl<0)return -3;
    static u8 rb[4096]; int rl=0, ro=0; int http=escl_req("POST","/eSCL/ScanJobs",job,jl,rb,(int)sizeof rb,&rl,&ro);
    if(http!=201){ p=scopy(p,"Scanner refused the scan job (HTTP "); p=udec(p,(u32)(http<0?0:http)); p=scopy(p,")"); *p=0; return -4; }
    char loc[160], path[160]; if(!upp_hdr(rb,ro,"Location",loc,160)||!upp_path(loc,path,140)){ scopy(p,"Scanner sent no job location")[0]=0; return -5; }
    char* e=path; while(*e)e++; if(e>path&&e[-1]=='/')e--; scopy(e,"/NextDocument");
    int cap=24u<<20; u8* img=(u8*)kmalloc((u64)cap); if(!img){ scopy(p,"Not enough memory for the scan")[0]=0; return -6; }
    scopy(g_scan_status,"Scanning...")[0]=0;
    for(int t=0;t<20;t++){ http=escl_req("GET",path,0,0,img,cap,&rl,&ro); if(http!=503)break; }
    if(http!=200||rl<4){ kfree(img); p=scopy(p,"Scan failed (HTTP "); p=udec(p,(u32)(http<0?0:http)); p=scopy(p,")"); *p=0; return -7; }
    const u8* d=img+ro; int png=(d[0]==0x89&&d[1]=='P');
    static int sn=1; char nm[64],*q=nm; q=scopy(q,"Scan-"); q=udec(q,(u32)(sn++)); q=scopy(q,png?".png":".jpg"); *q=0;
    int k=pic_store(nm,d,rl); kfree(img);
    if(k<0){ scopy(p,"Scan done, but it could not be saved")[0]=0; return -8; }
    q=g_scan_last; q=scopy(q,nm); *q=0; p=scopy(p,"Saved to Pictures: "); p=scopy(p,nm); *p=0; klog("escl: scan saved"); return 0; }
