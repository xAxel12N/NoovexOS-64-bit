#include <nvx/_types.h>
size_t fwrite(const void*,size_t,size_t,FILE*); ssize_t write(int,const void*,size_t); void* malloc(size_t);
typedef struct { void (*put)(void*,const char*,int); void* ctx; int count; } __fsink;
static void __emit(__fsink* k,const char* s,int n){ if(n>0){ k->put(k->ctx,s,n); k->count+=n; } }
static void __padc(__fsink* k,char c,int n){ char b[32]; for(int i=0;i<32;i++)b[i]=c; while(n>0){ int m=n>32?32:n; __emit(k,b,m); n-=m; } }
static int __slen(const char* s){ int n=0; while(s[n])n++; return n; }
typedef struct { uint32_t d[130]; int n; } __big;
static void __bset(__big* b,uint64_t v){ b->n=0; while(v){ b->d[b->n++]=(uint32_t)(v%1000000000u); v/=1000000000u; } }
static void __bmul(__big* b,uint32_t m){ uint64_t c=0; for(int i=0;i<b->n;i++){ uint64_t t=(uint64_t)b->d[i]*m+c; b->d[i]=(uint32_t)(t%1000000000u); c=t/1000000000u; } while(c){ b->d[b->n++]=(uint32_t)(c%1000000000u); c/=1000000000u; } }
static int __bdigits(__big* b,char* out){ if(!b->n){ out[0]='0'; return 1; } int o=0; uint32_t v=b->d[b->n-1]; char t[10]; int tn=0; while(v){ t[tn++]=(char)('0'+v%10); v/=10; } while(tn)out[o++]=t[--tn];
    for(int i=b->n-2;i>=0;i--){ uint32_t w=b->d[i]; for(int k=8;k>=0;k--){ out[o+k]=(char)('0'+w%10); w/=10; } o+=9; } return o; }
static int __exact(double x,char* dig,int* shift){
    union { double d; uint64_t u; } u={x}; uint64_t m=u.u&0xFFFFFFFFFFFFFull; int e=(int)((u.u>>52)&0x7FF);
    if(e){ m|=1ull<<52; e-=1075; } else e=-1074;
    __big b; __bset(&b,m);
    if(e>=0){ int k=e; while(k>=29){ __bmul(&b,1u<<29); k-=29; } if(k)__bmul(&b,1u<<k); *shift=0; }
    else { int k=-e; while(k>=13){ __bmul(&b,1220703125u); k-=13; } uint32_t p=1; while(k--)p*=5; __bmul(&b,p); *shift=-e; }
    return __bdigits(&b,dig);
}
static int __round(char* d,int len,int keep){
    if(keep>=len){ while(len<keep)d[len++]='0'; return 0; }
    if(keep<0)return 0;
    int up=0; char nx=d[keep];
    if(nx>'5')up=1; else if(nx=='5'){ int rest=0; for(int i=keep+1;i<len;i++) if(d[i]!='0'){ rest=1; break; } up=rest?1:(keep>0?((d[keep-1]-'0')&1):0); }
    if(up){ int i=keep-1; while(i>=0){ if(d[i]=='9'){ d[i]='0'; i--; } else { d[i]++; break; } }
        if(i<0){ for(int j=keep;j>0;j--)d[j]=d[j-1]; d[0]='1'; return 1; } }
    return 0;
}
static char __fbuf[1200];
static int __fmt_f(double x,int prec,int alt,char* out){
    int sh; int L=__exact(x,__fbuf,&sh); char* d=__fbuf;
    int intd=L-sh; if(intd<1){ int z=1-intd; for(int i=L-1;i>=0;i--)d[i+z]=d[i]; for(int i=0;i<z;i++)d[i]='0'; L+=z; intd=1; }
    int keep=intd+prec; int carry=__round(d,L,keep); if(carry)intd++;
    int o=0; for(int i=0;i<intd;i++)out[o++]=d[i];
    if(prec>0||alt)out[o++]='.'; for(int i=0;i<prec;i++)out[o++]=d[intd+i]; out[o]=0; return o;
}
static int __fmt_e(double x,int prec,int alt,int upper,char* out,int* exp_out){
    int o=0; int E=0;
    if(x==0){ out[o++]='0'; if(prec>0||alt)out[o++]='.'; for(int i=0;i<prec;i++)out[o++]='0'; E=0; }
    else { int sh; int L=__exact(x,__fbuf,&sh); char* d=__fbuf; E=(L-sh)-1;
        int carry=__round(d,L,prec+1); if(carry)E++;
        out[o++]=d[0]; if(prec>0||alt)out[o++]='.'; for(int i=1;i<=prec;i++)out[o++]=d[i]; }
    if(exp_out){ *exp_out=E; }
    out[o++]=upper?'E':'e'; out[o++]=E<0?'-':'+'; int a=E<0?-E:E; char t[6]; int tn=0; do{ t[tn++]=(char)('0'+a%10); a/=10; }while(a); if(tn<2)t[tn++]='0'; while(tn)out[o++]=t[--tn]; out[o]=0; return o;
}
static int __fmt_g(double x,int prec,int alt,int upper,char* out){
    if(prec==0)prec=1;
    int E=0;
    if(x!=0){ char tmp[1300]; __fmt_e(x,prec-1,0,0,tmp,&E); }
    int o;
    if(E<prec&&E>=-4) o=__fmt_f(x,prec-1-E,alt,out);
    else o=__fmt_e(x,prec-1,alt,upper,out,0);
    if(!alt){ int dot=-1, ep=o; for(int i=0;i<o;i++){ if(out[i]=='.')dot=i; if(out[i]=='e'||out[i]=='E'){ ep=i; break; } }
        if(dot>=0){ int k=ep; while(k>dot+1&&out[k-1]=='0')k--; if(k==dot+1)k=dot; int tail=o-ep; for(int i=0;i<tail;i++)out[k+i]=out[ep+i]; o=k+tail; out[o]=0; } }
    return o;
}
static int __fmt_a(double x,int prec,int alt,int upper,char* out){
    union { double d; uint64_t u; } u={x}; uint64_t m=u.u&0xFFFFFFFFFFFFFull; int e=(int)((u.u>>52)&0x7FF); int lead=1;
    const char* hx=upper?"0123456789ABCDEF":"0123456789abcdef"; int o=0;
    if(!e&&!m){ lead=0; e=0; } else if(!e){ lead=0; e=-1022; } else e-=1023;
    if(prec>=0&&prec<13){ int drop=(13-prec)*4; uint64_t full=((uint64_t)lead<<52)|m; uint64_t rem=full&((1ull<<drop)-1), half=1ull<<(drop-1); full>>=drop;
        if(rem>half||(rem==half&&(full&1)))full++; lead=(int)(full>>(prec*4)); m=full&((prec?(1ull<<(prec*4)):1)-1); if(!prec)m=0;
        out[o++]=(char)('0'+lead); if(prec||alt)out[o++]='.'; for(int i=prec-1;i>=0;i--)out[o++]=hx[(m>>(i*4))&15]; }
    else { out[o++]=(char)('0'+lead); int nd=13; while(nd>0&&!((m>>((13-nd)*4))&15))nd--; int p2=prec<0?nd:prec;
        if(p2||alt)out[o++]='.'; for(int i=0;i<p2;i++){ int sft=(12-i)*4; out[o++]=i<13?hx[(m>>sft)&15]:'0'; } }
    out[o++]=upper?'P':'p'; out[o++]=e<0?'-':'+'; int a=e<0?-e:e; char t[6]; int tn=0; do{ t[tn++]=(char)('0'+a%10); a/=10; }while(a); while(tn)out[o++]=t[--tn]; out[o]=0; return o;
}
int __vfmt(__fsink* k,const char* f,va_list ap){
    char nb[1400];
    for(;*f;f++){
        if(*f!='%'){ const char* s=f; while(f[1]&&f[1]!='%')f++; __emit(k,s,(int)(f-s+1)); continue; }
        f++; if(*f=='%'){ __emit(k,"%",1); continue; }
        int left=0,plus=0,space=0,alt=0,zero=0;
        for(;;f++){ if(*f=='-')left=1; else if(*f=='+')plus=1; else if(*f==' ')space=1; else if(*f=='#')alt=1; else if(*f=='0')zero=1; else break; }
        int width=0; if(*f=='*'){ width=va_arg(ap,int); if(width<0){ left=1; width=-width; } f++; } else while(*f>='0'&&*f<='9')width=width*10+(*f++-'0');
        int prec=-1; if(*f=='.'){ f++; prec=0; if(*f=='*'){ prec=va_arg(ap,int); if(prec<0)prec=-1; f++; } else while(*f>='0'&&*f<='9')prec=prec*10+(*f++-'0'); }
        int len=0; if(*f=='h'){ len=1; f++; if(*f=='h'){ len=0x10; f++; } } else if(*f=='l'){ len=2; f++; if(*f=='l'){ len=3; f++; } } else if(*f=='j'||*f=='z'||*f=='t'){ len=3; f++; } else if(*f=='L'){ len=4; f++; } else if(*f=='q'){ len=3; f++; }
        char c=*f; if(!c)break;
        const char* body=nb; int blen=0; char sign=0; const char* pre=""; int prelen=0; int numeric=0;
        if(c=='d'||c=='i'){ long long v; if(len==3)v=va_arg(ap,long long); else if(len==2)v=va_arg(ap,long); else { v=va_arg(ap,int); if(len==1)v=(short)v; else if(len==0x10)v=(signed char)v; }
            unsigned long long u=v<0?0ull-(unsigned long long)v:(unsigned long long)v; if(v<0)sign='-'; else if(plus)sign='+'; else if(space)sign=' ';
            char t[24]; int tn=0; while(u){ t[tn++]=(char)('0'+u%10); u/=10; } int pd=prec<0?1:prec; while(tn<pd)t[tn++]='0'; for(int i=0;i<tn;i++)nb[i]=t[tn-1-i]; blen=tn; numeric=1; }
        else if(c=='u'||c=='o'||c=='x'||c=='X'||c=='b'){ unsigned long long u; if(len==3)u=va_arg(ap,unsigned long long); else if(len==2)u=va_arg(ap,unsigned long); else { u=va_arg(ap,unsigned int); if(len==1)u=(unsigned short)u; else if(len==0x10)u=(unsigned char)u; }
            int base=c=='o'?8:c=='u'?10:c=='b'?2:16; const char* hx=c=='X'?"0123456789ABCDEF":"0123456789abcdef"; unsigned long long w=u;
            char t[70]; int tn=0; while(w){ t[tn++]=hx[w%base]; w/=base; } int pd=prec<0?1:prec; while(tn<pd)t[tn++]='0';
            if(c=='o'&&alt&&t[tn-1]!='0')t[tn++]='0';
            for(int i=0;i<tn;i++)nb[i]=t[tn-1-i]; blen=tn; numeric=1;
            if(alt&&u&&(c=='x'||c=='X')){ pre=c=='x'?"0x":"0X"; prelen=2; } }
        else if(c=='c'){ nb[0]=(char)va_arg(ap,int); blen=1; }
        else if(c=='s'){ const char* s=va_arg(ap,const char*); if(!s)s=(prec<0||prec>=6)?"(null)":""; int n=0; while(s[n]&&(prec<0||n<prec))n++; body=s; blen=n; }
        else if(c=='p'){ void* p=va_arg(ap,void*); if(!p){ body="(nil)"; blen=5; } else { unsigned long long u=(unsigned long long)(uintptr_t)p; char t[20]; int tn=0; while(u){ t[tn++]="0123456789abcdef"[u&15]; u>>=4; } for(int i=0;i<tn;i++)nb[i]=t[tn-1-i]; blen=tn; pre="0x"; prelen=2; if(plus)sign='+'; else if(space)sign=' '; } }
        else if(c=='n'){ int* p=va_arg(ap,int*); if(p)*p=k->count; continue; }
        else if(c=='f'||c=='F'||c=='e'||c=='E'||c=='g'||c=='G'||c=='a'||c=='A'){
            double x=len==4?(double)va_arg(ap,long double):va_arg(ap,double);
            union { double d; uint64_t u; } u={x}; int neg=(int)(u.u>>63); if(neg){ x=-x; sign='-'; } else if(plus)sign='+'; else if(space)sign=' ';
            int up=(c=='F'||c=='E'||c=='G'||c=='A');
            if(x!=x){ body=up?"NAN":"nan"; blen=3; zero=0; }
            else if(x>1.7976931348623157e308){ body=up?"INF":"inf"; blen=3; zero=0; }
            else { numeric=2; if(c=='f'||c=='F')blen=__fmt_f(x,prec<0?6:prec,alt,nb); else if(c=='e'||c=='E')blen=__fmt_e(x,prec<0?6:prec,alt,up,nb,0); else if(c=='g'||c=='G')blen=__fmt_g(x,prec<0?6:prec,alt,up,nb); else { blen=__fmt_a(x,prec,alt,up,nb); pre=up?"0X":"0x"; prelen=2; } } }
        else { __emit(k,"%",1); __emit(k,f,1); continue; }
        int total=(sign?1:0)+prelen+blen; int padn=width>total?width-total:0;
        int zp=zero&&!left&&numeric&&(numeric==2||prec<0);
        if(!left&&!zp)__padc(k,' ',padn);
        if(sign)__emit(k,&sign,1); __emit(k,pre,prelen);
        if(zp)__padc(k,'0',padn);
        __emit(k,body,blen);
        if(left)__padc(k,' ',padn);
    }
    return k->count;
}
typedef struct { char* b; size_t cap; size_t n; } __sbk;
static void __sput(void* c,const char* s,int n){ __sbk* b=(__sbk*)c; for(int i=0;i<n;i++){ if(b->n+1<b->cap)b->b[b->n]=s[i]; b->n++; } }
int vsnprintf(char* out,size_t cap,const char* f,va_list ap){ __sbk b={out,cap,0}; __fsink k={__sput,&b,0}; int r=__vfmt(&k,f,ap); if(cap)out[b.n<cap?b.n:cap-1]=0; return r; }
int snprintf(char* out,size_t cap,const char* f,...){ va_list ap; va_start(ap,f); int r=vsnprintf(out,cap,f,ap); va_end(ap); return r; }
int vsprintf(char* out,const char* f,va_list ap){ return vsnprintf(out,(size_t)1<<30,f,ap); }
int sprintf(char* out,const char* f,...){ va_list ap; va_start(ap,f); int r=vsnprintf(out,(size_t)1<<30,f,ap); va_end(ap); return r; }
static void __fput(void* c,const char* s,int n){ fwrite(s,1,(size_t)n,(FILE*)c); }
static void __dput(void* c,const char* s,int n){ write((int)(intptr_t)c,s,(size_t)n); }
int vfprintf(FILE* fp,const char* f,va_list ap){ __fsink k={__fput,fp,0}; return __vfmt(&k,f,ap); }
int fprintf(FILE* fp,const char* f,...){ va_list ap; va_start(ap,f); int r=vfprintf(fp,f,ap); va_end(ap); return r; }
int vprintf(const char* f,va_list ap){ return vfprintf(stdout,f,ap); }
int printf(const char* f,...){ va_list ap; va_start(ap,f); int r=vfprintf(stdout,f,ap); va_end(ap); return r; }
int vdprintf(int fd,const char* f,va_list ap){ __fsink k={__dput,(void*)(intptr_t)fd,0}; return __vfmt(&k,f,ap); }
int dprintf(int fd,const char* f,...){ va_list ap; va_start(ap,f); int r=vdprintf(fd,f,ap); va_end(ap); return r; }
int vdprintf_impl(int fd,const char* f,va_list ap){ return vdprintf(fd,f,ap); }
int vfprintf_f(FILE* fp,const char* f,va_list ap){ return vfprintf(fp,f,ap); }
int __stdout_vprintf(const char* f,va_list ap){ return vfprintf(stdout,f,ap); }
int vasprintf(char** out,const char* f,va_list ap){ va_list a2; va_copy(a2,ap); int n=vsnprintf(0,0,f,a2); va_end(a2); if(n<0)return -1; char* b=(char*)malloc((size_t)n+1); if(!b)return -1; vsnprintf(b,(size_t)n+1,f,ap); *out=b; return n; }
int asprintf(char** out,const char* f,...){ va_list ap; va_start(ap,f); int r=vasprintf(out,f,ap); va_end(ap); return r; }
