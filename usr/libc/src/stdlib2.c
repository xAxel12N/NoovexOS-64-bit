#include <nvx/_types.h>
int fgetc(FILE*); int ungetc(int,FILE*); extern FILE __stdin;
int isspace(int c){ return c==' '||(c>='\t'&&c<='\r'); }
static int __dv(int c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='z')return c-'a'+10; if(c>='A'&&c<='Z')return c-'A'+10; return 99; }
static unsigned long long __strtox(const char* s,char** end,int base,int* neg,int* ovf){
    const char* p=s; *neg=0; *ovf=0; while(isspace((unsigned char)*p))p++;
    if(*p=='+'||*p=='-'){ *neg=(*p=='-'); p++; }
    if(base<0||base==1||base>36){ if(end)*end=(char*)s; errno=EINVAL; return 0; }
    if((base==0||base==16)&&p[0]=='0'&&(p[1]=='x'||p[1]=='X')&&__dv((unsigned char)p[2])<16){ p+=2; base=16; }
    else if(base==0)base=(p[0]=='0')?8:10;
    unsigned long long v=0; int any=0;
    for(;;p++){ int d=__dv((unsigned char)*p); if(d>=base)break; any=1;
        if(!*ovf){ if(v>(~0ull-(unsigned)d)/(unsigned)base)*ovf=1; else v=v*(unsigned)base+(unsigned)d; } }
    if(end)*end=(char*)(any?p:s);
    return any?v:0;
}
long long strtoll(const char* s,char** e,int b){ int n,o; unsigned long long v=__strtox(s,e,b,&n,&o);
    if(!n){ if(o||v>0x7FFFFFFFFFFFFFFFull){ errno=ERANGE; return 0x7FFFFFFFFFFFFFFFll; } return (long long)v; }
    if(o||v>0x8000000000000000ull){ errno=ERANGE; return (long long)0x8000000000000000ull; } return (long long)(0ull-v); }
unsigned long long strtoull(const char* s,char** e,int b){ int n,o; unsigned long long v=__strtox(s,e,b,&n,&o); if(o){ errno=ERANGE; return ~0ull; } return n?0ull-v:v; }
long strtol(const char* s,char** e,int b){ return (long)strtoll(s,e,b); }
unsigned long strtoul(const char* s,char** e,int b){ return (unsigned long)strtoull(s,e,b); }
long long strtoimax(const char* s,char** e,int b){ return strtoll(s,e,b); }
unsigned long long strtoumax(const char* s,char** e,int b){ return strtoull(s,e,b); }
int atoi(const char* s){ return (int)strtol(s,0,10); }
long atol(const char* s){ return strtol(s,0,10); }
long long atoll(const char* s){ return strtoll(s,0,10); }
#define DMAX 800
typedef struct { char d[DMAX]; int nd, dp, neg, trunc; } __dec;
static void __trim(__dec* a){ while(a->nd>0&&a->d[a->nd-1]=='0')a->nd--; if(a->nd==0)a->dp=0; }
static void __rsh(__dec* a,unsigned k){
    int r=0,w=0; unsigned long long n=0;
    for(;(n>>k)==0;r++){ if(r>=a->nd){ if(n==0){ a->nd=0; return; } while((n>>k)==0){ n*=10; r++; } break; } n=n*10+(unsigned long long)(a->d[r]-'0'); }
    a->dp-=r-1; unsigned long long mask=(1ull<<k)-1;
    for(;r<a->nd;r++){ unsigned long long c=(unsigned long long)(a->d[r]-'0'); unsigned long long dig=n>>k; n&=mask; a->d[w++]=(char)('0'+dig); n=n*10+c; }
    while(n>0){ unsigned long long dig=n>>k; n&=mask; if(w<DMAX)a->d[w++]=(char)('0'+dig); else if(dig>0)a->trunc=1; n*=10; }
    a->nd=w; __trim(a);
}
static void __lsh(__dec* a,unsigned k){
    static unsigned char t[DMAX+40]; int w=0; unsigned long long n=0;
    for(int r=a->nd-1;r>=0;r--){ n+=(unsigned long long)(a->d[r]-'0')<<k; t[w++]=(unsigned char)(n%10); n/=10; }
    while(n){ t[w++]=(unsigned char)(n%10); n/=10; }
    int delta=w-a->nd, o=0;
    for(int i=w-1;i>=0;i--){ if(o<DMAX)a->d[o++]=(char)('0'+t[i]); else if(t[i])a->trunc=1; }
    a->nd=o; a->dp+=delta; __trim(a);
}
static void __shift(__dec* a,int k){ if(a->nd==0)return; if(k>0){ while(k>60){ __lsh(a,60); k-=60; } __lsh(a,(unsigned)k); } else if(k<0){ while(k<-60){ __rsh(a,60); k+=60; } __rsh(a,(unsigned)-k); } }
static int __rup(__dec* a,int nd){ if(nd<0||nd>=a->nd)return 0; if(a->d[nd]=='5'&&nd+1==a->nd){ if(a->trunc)return 1; return nd>0&&((a->d[nd-1]-'0')&1); } return a->d[nd]>='5'; }
static unsigned long long __rint(__dec* a){ if(a->dp>20)return ~0ull; int i; unsigned long long n=0; for(i=0;i<a->dp&&i<a->nd;i++)n=n*10+(unsigned long long)(a->d[i]-'0'); for(;i<a->dp;i++)n*=10; if(__rup(a,a->dp))n++; return n; }
static double __dec2d(__dec* d,int* range){
    static const int pw[]={1,3,6,9,13,16,19,23,26}; int exp; unsigned long long mant; int bias=-1023;
    if(d->nd==0){ mant=0; exp=bias; goto out; }
    if(d->dp>310)goto ovf;
    if(d->dp<-330){ mant=0; exp=bias; *range=1; goto out; }
    exp=0;
    while(d->dp>0){ int n=d->dp>=9?27:pw[d->dp]; __shift(d,-n); exp+=n; }
    while(d->dp<0||(d->dp==0&&d->d[0]<'5')){ int n=-d->dp>=9?27:pw[-d->dp]; __shift(d,n); exp-=n; }
    exp--;
    if(exp<bias+1){ int n=bias+1-exp; __shift(d,-n); exp+=n; }
    if(exp-bias>=2047)goto ovf;
    __shift(d,53); mant=__rint(d);
    if(mant==(2ull<<52)){ mant>>=1; exp++; if(exp-bias>=2047)goto ovf; }
    if(!(mant&(1ull<<52))){ exp=bias; if(mant==0||1)*range=1; }
    goto out;
ovf: mant=0; exp=2047+bias; *range=1;
out:;
    unsigned long long bits=mant&((1ull<<52)-1); bits|=(unsigned long long)((exp-bias)&2047)<<52; if(d->neg)bits|=1ull<<63;
    union{ unsigned long long u; double f; } u; u.u=bits; return u.f;
}
static int __lc(int c){ return (c>='A'&&c<='Z')?c+32:c; }
static int __pre(const char* p,const char* w){ int i=0; while(w[i]){ if(__lc((unsigned char)p[i])!=w[i])return 0; i++; } return i; }
double strtod(const char* s,char** end){
    const char* p=s; while(isspace((unsigned char)*p))p++; int neg=0; if(*p=='+'||*p=='-'){ neg=(*p=='-'); p++; }
    union{ unsigned long long u; double f; } u; int n;
    if((n=__pre(p,"infinity"))||(n=__pre(p,"inf"))){ if(end)*end=(char*)(p+n); u.u=0x7FF0000000000000ull|(neg?1ull<<63:0); return u.f; }
    if((n=__pre(p,"nan"))){ p+=n; if(*p=='('){ const char* q=p+1; while((*q>='0'&&*q<='9')||(__lc((unsigned char)*q)>='a'&&__lc((unsigned char)*q)<='z')||*q=='_')q++; if(*q==')')p=q+1; } if(end)*end=(char*)p; u.u=0x7FF8000000000000ull|(neg?1ull<<63:0); return u.f; }
    if(p[0]=='0'&&(p[1]=='x'||p[1]=='X')&&(__dv((unsigned char)p[2])<16||(p[2]=='.'&&__dv((unsigned char)p[3])<16))){
        const char* q=p+2; unsigned long long m=0; int e=0, any=0, sticky=0, seen_dot=0;
        for(;;q++){ if(*q=='.'&&!seen_dot){ seen_dot=1; continue; } int dv=__dv((unsigned char)*q); if(dv>=16)break; any=1;
            if(m>>60){ if(dv)sticky=1; if(!seen_dot)e+=4; } else { m=m*16+(unsigned)dv; if(seen_dot)e-=4; } }
        if(*q=='p'||*q=='P'){ const char* r=q+1; int en=0; if(*r=='+'||*r=='-'){ en=(*r=='-'); r++; } if(*r>='0'&&*r<='9'){ int ev=0; while(*r>='0'&&*r<='9'){ if(ev<100000)ev=ev*10+(*r-'0'); r++; } e+=en?-ev:ev; q=r; } }
        if(end)*end=(char*)q; if(!m){ u.u=neg?1ull<<63:0; return u.f; }
        int top=63; while(!(m>>top))top--; int ex=e+top;
        if(ex>1023){ errno=ERANGE; u.u=0x7FF0000000000000ull|(neg?1ull<<63:0); return u.f; }
        int keep=ex>=-1022?52:52-(-1022-ex); if(keep<-1){ errno=ERANGE; u.u=neg?1ull<<63:0; return u.f; }
        int drop=top-keep; unsigned long long mm; 
        if(drop>0){ unsigned long long rem=drop>=64?m:(m&((1ull<<drop)-1)); mm=drop>=64?0:(m>>drop); unsigned long long half=drop>=65?0:(1ull<<(drop-1)); if(rem>half||(rem==half&&(sticky||(mm&1))))mm++; else if(rem==half&&!sticky&&!(mm&1)){} }
        else mm=m<<(-drop);
        if(ex>=-1022){ if(mm>>53){ mm>>=1; ex++; if(ex>1023){ errno=ERANGE; u.u=0x7FF0000000000000ull|(neg?1ull<<63:0); return u.f; } } u.u=((unsigned long long)(ex+1023)<<52)|(mm&((1ull<<52)-1)); }
        else { if(mm>>52)u.u=1ull<<52; else u.u=mm; }
        if(neg)u.u|=1ull<<63; return u.f; }
    static __dec d; d.nd=0; d.dp=0; d.neg=neg; d.trunc=0; int any=0, sawdot=0, ndig=0;
    for(;;p++){ if(*p=='.'&&!sawdot){ sawdot=1; d.dp=ndig; continue; } if(*p<'0'||*p>'9')break; any=1;
        if(*p=='0'&&d.nd==0){ if(sawdot)d.dp--; else {} continue; }
        ndig++; if(d.nd<DMAX)d.d[d.nd++]=*p; else if(*p!='0')d.trunc=1; }
    if(!sawdot)d.dp=ndig; else if(d.nd==0)d.dp=0;
    if(!any){ if(end)*end=(char*)s; return 0.0; }
    if(sawdot){ int lead0=0; }
    if((*p=='e'||*p=='E')){ const char* r=p+1; int en=0; if(*r=='+'||*r=='-'){ en=(*r=='-'); r++; } if(*r>='0'&&*r<='9'){ int ev=0; while(*r>='0'&&*r<='9'){ if(ev<100000)ev=ev*10+(*r-'0'); r++; } d.dp+=en?-ev:ev; p=r; } }
    if(end)*end=(char*)p; __trim(&d); int range=0; double v=__dec2d(&d,&range); if(range&&d.nd)errno=ERANGE; return v;
}
float strtof(const char* s,char** e){ return (float)strtod(s,e); }
long double strtold(const char* s,char** e){ return strtod(s,e); }
double atof(const char* s){ return strtod(s,0); }
