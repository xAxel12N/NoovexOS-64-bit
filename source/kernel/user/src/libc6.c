#include "posix.h"
static int cmpi(const void* a,const void* b,void* ctx){ (void)ctx; return *(const int*)a-*(const int*)b; }
int main(int argc,char** argv){
    printf("strtoll(-0x1F)=%ld strtod(3.25e2)=%f\n",(long)strtoll("-0x1F",0,0),strtod("3.25e2",0));
    char v1[]="a,b,c"; char* parts[4]; int n=strsplit(v1,',',parts,4); printf("strsplit n=%d last=%s\n",n,parts[n-1]);
    printf("strverscmp(v1.9,v1.10)=%d\n",strverscmp("v1.9","v1.10")<0?-1:1);
    printf("realpath(/home/../bin/./x)=%s\n",realpath("/home/../bin/./x",0));
    printf("tgamma(5)=%f erf(1)=%f asinh(1)=%f\n",tgamma(5.0),erf(1.0),asinh(1.0));
    int arr[5]={5,3,9,1,4}; qsort_r(arr,5,sizeof(int),cmpi,0); printf("qsort_r: %d %d %d %d %d\n",arr[0],arr[1],arr[2],arr[3],arr[4]);
    printf("crc32(\"123456789\")=%x\n",crc32(0,"123456789",9));
    char b64[32]; base64_encode("hello",5,b64); char dec[16]; long dl=base64_decode(b64,dec); dec[dl]=0; printf("base64: %s -> %s\n",b64,dec);
    char u[5]; int ul=utf8_encode(0x20AC,u); unsigned cp; utf8_decode(u,&cp); printf("utf8 euro: %d bytes, cp=%x\n",ul,cp);
    struct tm t={0,0,12,26,8,124,0,0,0}; printf("timegm(2024-09-26 12:00)=%ld\n",(long)timegm(&t));
    char* s=0; asprintf(&s,"%s-%d","nvx",64); printf("asprintf: %s\n",s);
    char host[32]; gethostname(host,32); printf("host=%s uid=%d login=%s\n",host,getuid(),getlogin());
    int c; optind=1; while((c=getopt(argc,argv,"vn:"))!=-1){ if(c=='v')printf("opt -v\n"); if(c=='n')printf("opt -n %s\n",optarg); }
    return 0;
}
