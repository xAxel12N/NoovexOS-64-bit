#include <stddef.h>
#include <stdarg.h>
#include <math.h>
typedef unsigned long size_t_;
extern void* memcpy(void*,const void*,unsigned long);
extern unsigned long strlen(const char*);
extern void lua_emit(const char*,int);

int errno=0;

/* ---------------- ctype ---------------- */
int isalpha(int c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z');}
int isdigit(int c){return c>='0'&&c<='9';}
int isalnum(int c){return isalpha(c)||isdigit(c);}
int isspace(int c){return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f';}
int isupper(int c){return c>='A'&&c<='Z';}
int islower(int c){return c>='a'&&c<='z';}
int isxdigit(int c){return isdigit(c)||(c>='a'&&c<='f')||(c>='A'&&c<='F');}
int iscntrl(int c){return (c>=0&&c<32)||c==127;}
int isprint(int c){return c>=32&&c<127;}
int isgraph(int c){return c>32&&c<127;}
int ispunct(int c){return isprint(c)&&c!=' '&&!isalnum(c);}
int toupper(int c){return islower(c)?c-32:c;}
int tolower(int c){return isupper(c)?c+32:c;}

/* ---------------- string extras ---------------- */
int strcoll(const char*a,const char*b){ while(*a&&*a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b; }
char* strpbrk(const char*s,const char*set){ for(;*s;s++){ for(const char*t=set;*t;t++) if(*s==*t) return (char*)s; } return 0; }
size_t strspn(const char*s,const char*set){ size_t n=0; for(;s[n];n++){ const char*t=set; while(*t&&*t!=s[n])t++; if(!*t)break; } return n; }
size_t strcspn(const char*s,const char*set){ size_t n=0; for(;s[n];n++){ const char*t=set; while(*t&&*t!=s[n])t++; if(*t)break; } return n; }
char* strncat(char*d,const char*s,size_t n){ char*r=d; while(*d)d++; while(n--&&*s)*d++=*s++; *d=0; return r; }
char* strerror(int e){ (void)e; return "error"; }
static char* g_tok=0;
char* strtok(char*s,const char*d){ if(s)g_tok=s; if(!g_tok)return 0; while(*g_tok){const char*t=d;int m=0;while(*t)if(*t++==*g_tok){m=1;break;} if(!m)break; g_tok++;} if(!*g_tok)return 0; char*st=g_tok; while(*g_tok){const char*t=d;int m=0;while(*t)if(*t++==*g_tok){m=1;break;} if(m){*g_tok++=0;return st;} g_tok++;} return st; }

/* ---------------- stdlib extras ---------------- */
long labs(long v){return v<0?-v:v;}
long atol(const char*s){ long n=0,neg=0; while(isspace(*s))s++; if(*s=='-'){neg=1;s++;}else if(*s=='+')s++; while(isdigit(*s))n=n*10+(*s++-'0'); return neg?-n:n; }
static unsigned long g_seed=123456789;
int rand(void){ g_seed=g_seed*1103515245+12345; return (int)((g_seed>>16)&0x7fffffff); }
void srand(unsigned s){ g_seed=s; }
char* getenv(const char*n){ (void)n; return 0; }
int system(const char*c){ (void)c; return -1; }
void abort(void){ for(;;){} }
void exit(int c){ (void)c; for(;;){} }
long strtol(const char*s,char**e,int base){ long n=0,neg=0; while(isspace(*s))s++; if(*s=='-'){neg=1;s++;}else if(*s=='+')s++; if((base==16||base==0)&&s[0]=='0'&&(s[1]=='x'||s[1]=='X')){s+=2;base=16;} if(base==0)base=10; for(;;){int d; if(isdigit(*s))d=*s-'0'; else if(*s>='a'&&*s<='z')d=*s-'a'+10; else if(*s>='A'&&*s<='Z')d=*s-'A'+10; else break; if(d>=base)break; n=n*base+d; s++;} if(e)*e=(char*)s; return neg?-n:n; }
unsigned long strtoul(const char*s,char**e,int base){ return (unsigned long)strtol(s,e,base); }
long long strtoll(const char*s,char**e,int base){ return strtol(s,e,base); }
unsigned long long strtoull(const char*s,char**e,int base){ return (unsigned long long)strtol(s,e,base); }
static void swp(char*a,char*b,size_t n){ while(n--){char t=*a;*a++=*b;*b++=t;} }
void qsort(void*base,size_t n,size_t sz,int(*cmp)(const void*,const void*)){ char*a=base; if(n<2)return; for(size_t i=1;i<n;i++)for(size_t j=i;j>0;j--){ if(cmp(a+(j-1)*sz,a+j*sz)>0) swp(a+(j-1)*sz,a+j*sz,sz); else break; } }
void* bsearch(const void*key,const void*base,size_t n,size_t sz,int(*cmp)(const void*,const void*)){ const char*a=base; size_t lo=0,hi=n; while(lo<hi){ size_t m=(lo+hi)/2; int c=cmp(key,a+m*sz); if(c<0)hi=m; else if(c>0)lo=m+1; else return (void*)(a+m*sz); } return 0; }

/* ---------------- locale ---------------- */
struct lconv { char* decimal_point; char* thousands_sep; char* grouping; };
static struct lconv g_lc={".","",""};
struct lconv* localeconv(void){ return &g_lc; }
char* setlocale(int c,const char*l){ (void)c;(void)l; return (char*)"C"; }

/* ---------------- time (stubs) ---------------- */
long time(long*t){ if(t)*t=0; return 0; }
long clock(void){ return 0; }

/* ---------------- math ---------------- */
static const double PI=3.14159265358979323846, PI2=6.28318530717958647692, HALFPI=1.57079632679489661923;
double fabs(double x){ return x<0?-x:x; }
double floor(double x){ long i=(long)x; if((double)i>x)i--; return (double)i; }
double ceil(double x){ long i=(long)x; if((double)i<x)i++; return (double)i; }
double trunc(double x){ return (double)(long)x; }
double round(double x){ return x<0?ceil(x-0.5):floor(x+0.5); }
double fmod(double a,double b){ if(b==0)return 0; double q=(double)(long)(a/b); return a-q*b; }
double copysign(double x,double y){ double a=fabs(x); return y<0?-a:a; }
double fmax(double a,double b){ return a>b?a:b; }
double fmin(double a,double b){ return a<b?a:b; }
double sqrt(double x){ if(x<=0)return 0; double r=x,p=0; int g=0; while(r!=p&&g++<200){p=r;r=(r+x/r)*0.5;} return r; }
double ldexp(double x,int e){ double r=x; if(e>0)while(e--)r*=2; else while(e++)r/=2; return r; }
double frexp(double x,int*e){ int n=0; if(x==0){*e=0;return 0;} double a=fabs(x); while(a>=1){a/=2;n++;} while(a<0.5){a*=2;n--;} *e=n; return x<0?-a:a; }
double modf(double x,double*ip){ double i=trunc(x); *ip=i; return x-i; }
double exp(double x){ if(x>709)return HUGE_VAL; if(x<-709)return 0; int k=(int)(x/0.6931471805599453+(x<0?-0.5:0.5)); double r=x-k*0.6931471805599453; double t=1,s=1; for(int n=1;n<20;n++){ t*=r/n; s+=t; } return ldexp(s,k); }
double log(double x){ if(x<=0)return -HUGE_VAL; int e; double m=frexp(x,&e); double z=(m-1)/(m+1),z2=z*z,s=0,p=z; for(int n=1;n<40;n+=2){ s+=p/n; p*=z2; } return 2*s+e*0.6931471805599453; }
double log2(double x){ return log(x)/0.6931471805599453; }
double log10(double x){ return log(x)/2.302585092994046; }
double pow(double b,double e){ if(b==0)return e==0?1:0; if(e==0)return 1; if(b<0){ long ei=(long)e; if((double)ei==e){ double r=exp(e*log(-b)); return (ei&1)?-r:r; } return 0; } return exp(e*log(b)); }
double sin(double x){ x=fmod(x,PI2); if(x<-PI)x+=PI2; if(x>PI)x-=PI2; double t=x,s=x,x2=x*x; for(int n=1;n<12;n++){ t*=-x2/((2*n)*(2*n+1)); s+=t; } return s; }
double cos(double x){ return sin(x+HALFPI); }
double tan(double x){ double c=cos(x); return c==0?HUGE_VAL:sin(x)/c; }
double atan(double x){ int neg=x<0; if(neg)x=-x; int inv=x>1; if(inv)x=1/x; double t=x,s=x,x2=x*x; for(int n=1;n<40;n++){ t*=-x2; s+=t/(2*n+1); } if(inv)s=HALFPI-s; return neg?-s:s; }
double atan2(double y,double x){ if(x>0)return atan(y/x); if(x<0){ if(y>=0)return atan(y/x)+PI; return atan(y/x)-PI; } if(y>0)return HALFPI; if(y<0)return -HALFPI; return 0; }
double asin(double x){ if(x<=-1)return -HALFPI; if(x>=1)return HALFPI; return atan(x/sqrt(1-x*x)); }
double acos(double x){ return HALFPI-asin(x); }
double sinh(double x){ return (exp(x)-exp(-x))/2; }
double cosh(double x){ return (exp(x)+exp(-x))/2; }
double tanh(double x){ double e=exp(2*x); return (e-1)/(e+1); }

/* ---------------- strtod / atof ---------------- */
double strtod(const char*s,char**end){ while(isspace(*s))s++; int neg=0; if(*s=='-'){neg=1;s++;}else if(*s=='+')s++;
    double r=0; int any=0; while(isdigit(*s)){ r=r*10+(*s-'0'); s++; any=1; }
    if(*s=='.'){ s++; double f=0.1; while(isdigit(*s)){ r+=(*s-'0')*f; f*=0.1; s++; any=1; } }
    if((*s=='e'||*s=='E')&&any){ s++; int eneg=0; if(*s=='-'){eneg=1;s++;}else if(*s=='+')s++; int ex=0; while(isdigit(*s)){ex=ex*10+(*s-'0');s++;} double p=1; while(ex--)p*=10; if(eneg)r/=p; else r*=p; }
    if(end)*end=(char*)s; return neg?-r:r; }
double atof(const char*s){ return strtod(s,0); }

/* ---------------- vsnprintf (incl. %f %g %e) ---------------- */
static int u2s(char*b,unsigned long v,int base,int upper){ char t[32]; int i=0; const char*d=upper?"0123456789ABCDEF":"0123456789abcdef"; if(!v)t[i++]='0'; while(v){t[i++]=d[v%base];v/=base;} int n=i; while(i)*b++=t[--i]; return n; }
static int d2s(char*b,double v,int prec,char mode){ char*st=b;
    if(v!=v){ *b++='n';*b++='a';*b++='n';return 3; }
    if(v<0){*b++='-';v=-v;} if(v>1e308){*b++='i';*b++='n';*b++='f';return (int)(b-st);}
    if(prec<0)prec=6;
    if(mode=='g'){ int P=prec?prec:1; int e=0; double t=v; if(v!=0){ while(t>=10){t/=10;e++;} while(t<1){t*=10;e--;} }
        if(e< -4 || e>=P){ /* %e style */ double m=v; if(m!=0){ if(e>0){for(int i=0;i<e;i++)m/=10;} else {for(int i=0;i<-e;i++)m*=10;} }
            b+=d2s(b,m,P-1,'f'); /* strip trailing zeros */ char*p=b-1; while(*p=='0')p--; if(*p=='.')p--; b=p+1; *b++='e'; if(e<0){*b++='-';e=-e;}else *b++='+'; if(e<10)*b++='0'; b+=u2s(b,(unsigned long)e,10,0); return (int)(b-st); }
        prec=P-1-e; if(prec<0)prec=0; mode='f'; }
    unsigned long ip=(unsigned long)v; double fp=v-(double)ip;
    b+=u2s(b,ip,10,0);
    if(prec>0){ *b++='.'; for(int i=0;i<prec;i++){ fp*=10; int dg=(int)fp; if(dg<0)dg=0; if(dg>9)dg=9; *b++=(char)('0'+dg); fp-=dg; } }
    return (int)(b-st); }
int vsnprintf(char*out,size_t cap,const char*f,va_list ap){ size_t o=0; char tmp[400];
    #define PUT(ch) do{ if(o+1<cap)out[o]=(char)(ch); o++; }while(0)
    for(;*f;f++){ if(*f!='%'){ PUT(*f); continue; } f++;
        int left=0,zero=0,plus=0,space=0,alt=0; for(;;f++){ if(*f=='-')left=1; else if(*f=='0')zero=1; else if(*f=='+')plus=1; else if(*f==' ')space=1; else if(*f=='#')alt=1; else break; }
        int width=0; if(*f=='*'){ width=va_arg(ap,int); f++; } else while(isdigit(*f)){ width=width*10+(*f-'0'); f++; }
        int prec=-1; if(*f=='.'){ f++; prec=0; if(*f=='*'){ prec=va_arg(ap,int); f++; } else while(isdigit(*f)){ prec=prec*10+(*f-'0'); f++; } }
        while(*f=='l'||*f=='h'||*f=='z'||*f=='j'||*f=='t'||*f=='L')f++;
        char c=*f; int len=0; char* src=tmp; int neg=0;
        if(c=='d'||c=='i'){ long v=va_arg(ap,long); if(v<0){neg=1;} unsigned long uv=neg?(unsigned long)(-v):(unsigned long)v; len=u2s(tmp,uv,10,0); }
        else if(c=='u'){ unsigned long v=va_arg(ap,unsigned long); len=u2s(tmp,v,10,0); }
        else if(c=='x'){ unsigned long v=va_arg(ap,unsigned long); len=u2s(tmp,v,16,0); }
        else if(c=='X'){ unsigned long v=va_arg(ap,unsigned long); len=u2s(tmp,v,16,1); }
        else if(c=='o'){ unsigned long v=va_arg(ap,unsigned long); len=u2s(tmp,v,8,0); }
        else if(c=='p'){ unsigned long v=(unsigned long)va_arg(ap,void*); tmp[0]='0';tmp[1]='x'; len=2+u2s(tmp+2,v,16,0); }
        else if(c=='c'){ tmp[0]=(char)va_arg(ap,int); len=1; }
        else if(c=='s'){ src=va_arg(ap,char*); if(!src)src=(char*)"(null)"; len=0; while(src[len]&&(prec<0||len<prec))len++; }
        else if(c=='f'||c=='F'){ double v=va_arg(ap,double); if(v<0){neg=1;} len=d2s(tmp,v,prec,'f'); if(tmp[0]=='-'){src=tmp; neg=0;} }
        else if(c=='g'||c=='G'){ double v=va_arg(ap,double); len=d2s(tmp,v,prec<0?6:prec,'g'); }
        else if(c=='e'||c=='E'){ double v=va_arg(ap,double); len=d2s(tmp,v,prec<0?6:prec,'g'); }
        else if(c=='%'){ tmp[0]='%'; len=1; }
        else { PUT('%'); PUT(c); continue; }
        int sign=(neg&&(c=='d'||c=='i'))?1:0; int total=len+sign+((plus||space)&&(c=='d'||c=='i')&&!neg?1:0);
        int pad=width-total; if(pad<0)pad=0;
        if(!left && !zero) while(pad-->0)PUT(' ');
        if(sign)PUT('-'); else if((c=='d'||c=='i')&&plus)PUT('+'); else if((c=='d'||c=='i')&&space)PUT(' ');
        if(!left && zero) while(pad-->0)PUT('0');
        for(int i=0;i<len;i++)PUT(src[i]);
        if(left) while(pad-->0)PUT(' ');
    }
    if(cap)out[o<cap?o:cap-1]=0; return (int)o;
    #undef PUT
}
int snprintf(char*b,size_t n,const char*f,...){ va_list a; va_start(a,f); int r=vsnprintf(b,n,f,a); va_end(a); return r; }
int sprintf(char*b,const char*f,...){ va_list a; va_start(a,f); int r=vsnprintf(b,0x7fffffff,f,a); va_end(a); return r; }
int vsprintf(char*b,const char*f,va_list a){ return vsnprintf(b,0x7fffffff,f,a); }

/* ---------------- output (routed to NoovexOS) ---------------- */
typedef struct _NVXFILE FILE;
FILE* stdin=(FILE*)1; FILE* stdout=(FILE*)2; FILE* stderr=(FILE*)3;
int printf(const char*f,...){ char b[1024]; va_list a; va_start(a,f); int r=vsnprintf(b,sizeof(b),f,a); va_end(a); lua_emit(b,r>(int)sizeof(b)-1?(int)sizeof(b)-1:r); return r; }
int fprintf(FILE*fp,const char*f,...){ (void)fp; char b[1024]; va_list a; va_start(a,f); int r=vsnprintf(b,sizeof(b),f,a); va_end(a); lua_emit(b,r>(int)sizeof(b)-1?(int)sizeof(b)-1:r); return r; }
int vfprintf(FILE*fp,const char*f,va_list a){ (void)fp; char b[1024]; int r=vsnprintf(b,sizeof(b),f,a); lua_emit(b,r>(int)sizeof(b)-1?(int)sizeof(b)-1:r); return r; }
int fputs(const char*s,FILE*fp){ (void)fp; lua_emit(s,(int)strlen(s)); return 0; }
int fputc(int c,FILE*fp){ (void)fp; char b=(char)c; lua_emit(&b,1); return c; }
int putc(int c,FILE*fp){ return fputc(c,fp); }
int fflush(FILE*fp){ (void)fp; return 0; }
int ferror(FILE*fp){ (void)fp; return 0; }
int feof(FILE*fp){ (void)fp; return 1; }
/* io stubs (io library is disabled) */
FILE* fopen(const char*a,const char*b){ (void)a;(void)b; return 0; }
FILE* freopen(const char*a,const char*b,FILE*c){ (void)a;(void)b; return c; }
int fclose(FILE*f){ (void)f; return 0; }
size_t fread(void*p,size_t s,size_t n,FILE*f){ (void)p;(void)s;(void)n;(void)f; return 0; }
size_t fwrite(const void*p,size_t s,size_t n,FILE*f){ (void)f; lua_emit((const char*)p,(int)(s*n)); return n; }
int fseek(FILE*f,long o,int w){ (void)f;(void)o;(void)w; return -1; }
long ftell(FILE*f){ (void)f; return -1; }
char* fgets(char*b,int n,FILE*f){ (void)b;(void)n;(void)f; return 0; }
int getc(FILE*f){ (void)f; return -1; }
int fgetc(FILE*f){ (void)f; return -1; }
int ungetc(int c,FILE*f){ (void)f; return c; }
void clearerr(FILE*f){ (void)f; }
int setvbuf(FILE*f,char*b,int m,size_t s){ (void)f;(void)b;(void)m;(void)s; return 0; }
void setbuf(FILE*f,char*b){ (void)f;(void)b; }
int remove(const char*p){ (void)p; return -1; }
int rename(const char*a,const char*b){ (void)a;(void)b; return -1; }
FILE* tmpfile(void){ return 0; }
char* tmpnam(char*b){ (void)b; return 0; }
struct tm { int tm_sec,tm_min,tm_hour,tm_mday,tm_mon,tm_year,tm_wday,tm_yday,tm_isdst; };
double difftime(long a,long b){ return (double)(a-b); }
static struct tm g_tm;
struct tm* gmtime(const long* t){ (void)t; g_tm.tm_year=126; g_tm.tm_mon=0; g_tm.tm_mday=1; return &g_tm; }
struct tm* localtime(const long* t){ return gmtime(t); }
long mktime(struct tm* t){ (void)t; return 0; }
unsigned long strftime(char* s,unsigned long n,const char* f,const struct tm* t){ (void)f;(void)t; if(n){s[0]=0;} return 0; }
