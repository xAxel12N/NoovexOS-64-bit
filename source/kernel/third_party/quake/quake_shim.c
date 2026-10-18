
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>


static void (*qsh_sink)(const char*,int)=0;
void qsh_set_output(void (*fn)(const char*,int)){ qsh_sink=fn; }
static void qsh_emit(const char* s,int n){ if(qsh_sink)qsh_sink(s,n); }


static unsigned char* qsh_heap=0; static size_t QSH_HEAP=0;
void qsh_set_heap(void* p,size_t n){ qsh_heap=(unsigned char*)p; QSH_HEAP=n; }
typedef struct blk { size_t size; int used; struct blk* next; } blk;
static blk* qsh_free0=0; static int qsh_init=0;
static int qsh_heap_init(void){ if(!qsh_heap)return 0; qsh_free0=(blk*)qsh_heap; qsh_free0->size=QSH_HEAP-sizeof(blk); qsh_free0->used=0; qsh_free0->next=0; qsh_init=1; return 1; }
static long qsh_allocd=0;
void* malloc(size_t n){ if(!qsh_init&&!qsh_heap_init())return 0; if(n==0)n=1; n=(n+15)&~((size_t)15);
    for(blk* b=qsh_free0;b;b=b->next){ if(!b->used && b->size>=n){ if(b->size>=n+sizeof(blk)+32){ blk* nb=(blk*)((unsigned char*)b+sizeof(blk)+n); nb->size=b->size-n-sizeof(blk); nb->used=0; nb->next=b->next; b->next=nb; b->size=n; } b->used=1; qsh_allocd+=n; return (unsigned char*)b+sizeof(blk); } }
    { char m[96]; int L=0; const char* s="MALLOC FAIL: need "; while(s[L]){m[L]=s[L];L++;} char t[16]; int tn=0; unsigned long u=n; if(!u)t[tn++]='0'; while(u){t[tn++]='0'+u%10;u/=10;} while(tn)m[L++]=t[--tn]; const char* s2=" bytes, allocd "; for(int i=0;s2[i];i++)m[L++]=s2[i]; tn=0; u=(unsigned long)qsh_allocd; if(!u)t[tn++]='0'; while(u){t[tn++]='0'+u%10;u/=10;} while(tn)m[L++]=t[--tn]; m[L++]='\n'; qsh_emit(m,L); }
    return 0; }
void free(void* p){ if(!p)return; blk* b=(blk*)((unsigned char*)p-sizeof(blk)); b->used=0;
    for(blk* c=qsh_free0;c;c=c->next){ while(!c->used && c->next && !c->next->used){ c->size+=sizeof(blk)+c->next->size; c->next=c->next->next; } } }
static size_t qsh_bsz(void* p){ blk* b=(blk*)((unsigned char*)p-sizeof(blk)); return b->size; }
void* realloc(void* p,size_t n){ if(!p)return malloc(n); if(n==0){ free(p); return 0; } size_t old=qsh_bsz(p); if(old>=n)return p; void* q=malloc(n); if(!q)return 0; unsigned char* a=(unsigned char*)q,*b=(unsigned char*)p; for(size_t i=0;i<old;i++)a[i]=b[i]; free(p); return q; }
void* calloc(size_t a,size_t b){ size_t n=a*b; void* p=malloc(n); if(p){ unsigned char* q=(unsigned char*)p; for(size_t i=0;i<n;i++)q[i]=0; } return p; }


void* memcpy(void* d,const void* s,size_t n){ void* r=d; __asm__ volatile("rep movsb":"+D"(d),"+S"(s),"+c"(n)::"memory"); return r; }
void* memmove(void* d,const void* s,size_t n){ unsigned char* a=(unsigned char*)d; const unsigned char* b=(const unsigned char*)s; if(a<b){ for(size_t i=0;i<n;i++)a[i]=b[i]; } else { for(size_t i=n;i>0;i--)a[i-1]=b[i-1]; } return d; }
void* memset(void* d,int c,size_t n){ void* r=d; __asm__ volatile("rep stosb":"+D"(d),"+c"(n):"a"(c):"memory"); return r; }
int memcmp(const void* a,const void* b,size_t n){ const unsigned char* x=(const unsigned char*)a,*y=(const unsigned char*)b; for(size_t i=0;i<n;i++){ if(x[i]!=y[i])return x[i]-y[i]; } return 0; }
size_t strlen(const char* s){ size_t n=0; while(s[n])n++; return n; }
size_t strnlen(const char* s,size_t m){ size_t n=0; while(n<m&&s[n])n++; return n; }
char* strcpy(char* d,const char* s){ char* r=d; while((*d++=*s++)); return r; }
char* strncpy(char* d,const char* s,size_t n){ size_t i=0; for(;i<n&&s[i];i++)d[i]=s[i]; for(;i<n;i++)d[i]=0; return d; }
char* strcat(char* d,const char* s){ char* r=d; while(*d)d++; while((*d++=*s++)); return r; }
char* strncat(char* d,const char* s,size_t n){ char* r=d; while(*d)d++; size_t i=0; for(;i<n&&s[i];i++)d[i]=s[i]; d[i]=0; return r; }
int strcmp(const char* a,const char* b){ while(*a&&*a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b; }
int strncmp(const char* a,const char* b,size_t n){ for(size_t i=0;i<n;i++){ if(a[i]!=b[i])return (unsigned char)a[i]-(unsigned char)b[i]; if(!a[i])break; } return 0; }
static int qsh_lc(int c){ return (c>='A'&&c<='Z')?c+32:c; }
int strcasecmp(const char* a,const char* b){ while(*a&&qsh_lc(*a)==qsh_lc(*b)){a++;b++;} return qsh_lc((unsigned char)*a)-qsh_lc((unsigned char)*b); }
int strncasecmp(const char* a,const char* b,size_t n){ for(size_t i=0;i<n;i++){ int x=qsh_lc((unsigned char)a[i]),y=qsh_lc((unsigned char)b[i]); if(x!=y)return x-y; if(!a[i])break; } return 0; }
char* strchr(const char* s,int c){ while(*s){ if(*s==(char)c)return (char*)s; s++; } return (c==0)?(char*)s:0; }
char* strrchr(const char* s,int c){ const char* r=0; while(*s){ if(*s==(char)c)r=s; s++; } if(c==0)return (char*)s; return (char*)r; }
char* strstr(const char* h,const char* n){ if(!*n)return (char*)h; for(;*h;h++){ const char* a=h,*b=n; while(*a&&*b&&*a==*b){a++;b++;} if(!*b)return (char*)h; } return 0; }
char* strdup(const char* s){ size_t n=strlen(s)+1; char* p=(char*)malloc(n); if(p)for(size_t i=0;i<n;i++)p[i]=s[i]; return p; }


int toupper(int c){ return (c>='a'&&c<='z')?c-32:c; }
int tolower(int c){ return (c>='A'&&c<='Z')?c+32:c; }
int isspace(int c){ return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f'; }
int isdigit(int c){ return c>='0'&&c<='9'; }
int isxdigit(int c){ return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'); }
int isalpha(int c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); }
int isalnum(int c){ return isalpha(c)||isdigit(c); }
int isupper(int c){ return c>='A'&&c<='Z'; }
int islower(int c){ return c>='a'&&c<='z'; }
int iscntrl(int c){ return c<32||c==127; }
int isprint(int c){ return c>=32&&c<127; }


int atoi(const char* s){ int sgn=1,v=0; while(*s==' '||*s=='\t')s++; if(*s=='-'){sgn=-1;s++;} else if(*s=='+')s++; while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;} return sgn*v; }
long strtol(const char* s,char** end,int base){ long sgn=1,v=0; while(*s==' '||*s=='\t')s++; if(*s=='-'){sgn=-1;s++;} else if(*s=='+')s++; if(base==16&&s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2; if(base==0){ if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')){base=16;s+=2;} else base=10; } for(;;){ int d; char c=*s; if(c>='0'&&c<='9')d=c-'0'; else if(c>='a'&&c<='f')d=c-'a'+10; else if(c>='A'&&c<='F')d=c-'A'+10; else break; if(d>=base)break; v=v*base+d; s++; } if(end)*end=(char*)s; return sgn*v; }
double atof(const char* s){ int sgn=1; long ip=0,fp=0,fd=1; while(*s==' ')s++; if(*s=='-'){sgn=-1;s++;} else if(*s=='+')s++; while(*s>='0'&&*s<='9'){ip=ip*10+(*s-'0');s++;} if(*s=='.'){s++; while(*s>='0'&&*s<='9'){fp=fp*10+(*s-'0');fd*=10;s++;}} return sgn*((double)ip+(double)fp/(double)fd); }
int abs(int x){ return x<0?-x:x; }
long labs(long x){ return x<0?-x:x; }


double fabs(double x){ return x<0?-x:x; }
double floor(double x){ long i=(long)x; if((double)i>x)i--; return (double)i; }
double ceil(double x){ long i=(long)x; if((double)i<x)i++; return (double)i; }
double sqrt(double x){ if(x<=0)return 0; double r; __asm__("sqrtsd %1,%0":"=x"(r):"x"(x)); return r; }
static double qsh_sin(double x){ const double PI=3.14159265358979323846; while(x>PI)x-=2*PI; while(x<-PI)x+=2*PI; double x2=x*x,t=x,s=x; for(int n=1;n<10;n++){ t*=-x2/((2*n)*(2*n+1)); s+=t; } return s; }
double sin(double x){ return qsh_sin(x); }
double cos(double x){ return qsh_sin(x+1.57079632679489661923); }
double tan(double x){ double c=cos(x); return c==0?0:sin(x)/c; }
double atan(double x){ int neg=0; if(x<0){neg=1;x=-x;} int inv=0; if(x>1){inv=1;x=1.0/x;} double x2=x*x,t=x,s=x; for(int n=1;n<12;n++){ t*=-x2; s+=t/(2*n+1); } double r=s; if(inv)r=1.57079632679489661923-r; return neg?-r:r; }
double atan2(double y,double x){ const double PI=3.14159265358979323846; if(x>0)return atan(y/x); if(x<0){ if(y>=0)return atan(y/x)+PI; return atan(y/x)-PI; } if(y>0)return PI/2; if(y<0)return -PI/2; return 0; }
double pow(double b,double e){ int n=(int)e; double r=1; if(n<0){ b=1.0/b; n=-n; } for(int i=0;i<n;i++)r*=b; return r; }
double exp(double x){ double t=1,s=1; for(int n=1;n<20;n++){ t*=x/n; s+=t; } return s; }
double log(double x){ if(x<=0)return 0; double y=(x-1)/(x+1),y2=y*y,t=y,s=0; for(int n=0;n<20;n++){ s+=t/(2*n+1); t*=y2; } return 2*s; }


static int qsh_utoa(unsigned long v,int base,int upper,char* out){ char t[32]; int n=0; const char* dig=upper?"0123456789ABCDEF":"0123456789abcdef"; if(v==0)t[n++]='0'; while(v){ t[n++]=dig[v%base]; v/=base; } for(int i=0;i<n;i++)out[i]=t[n-1-i]; out[n]=0; return n; }
int vsnprintf(char* buf,size_t cap,const char* fmt,va_list ap){ size_t o=0;
    #define PUT(c) do{ if(o+1<cap)buf[o]=(char)(c); o++; }while(0)
    for(const char* p=fmt;*p;p++){ if(*p!='%'){ PUT(*p); continue; } p++;
        int lng=0,left=0,zero=0,width=0,prec=-1; char pad=' ';
        while(*p=='-'||*p=='0'||*p=='+'||*p==' '||*p=='#'){ if(*p=='-')left=1; else if(*p=='0'){zero=1;pad='0';} p++; }
        while(*p>='0'&&*p<='9'){ width=width*10+(*p-'0'); p++; }
        if(*p=='.'){ p++; prec=0; while(*p>='0'&&*p<='9'){ prec=prec*10+(*p-'0'); p++; } }
        while(*p=='l'||*p=='h'||*p=='z'){ if(*p=='l')lng++; p++; }
        char tmp[48]; int tn=0; const char* src=tmp;
        char c=*p;
        if(c=='d'||c=='i'){ long v=lng?va_arg(ap,long):(long)va_arg(ap,int); int neg=v<0; unsigned long u=neg?(unsigned long)(-v):(unsigned long)v; char nb[32]; int nn=qsh_utoa(u,10,0,nb); int zr=(prec>nn)?(prec-nn):0; tn=0; if(neg)tmp[tn++]='-'; for(int i=0;i<zr;i++)tmp[tn++]='0'; for(int i=0;i<nn;i++)tmp[tn++]=nb[i]; tmp[tn]=0; }
        else if(c=='u'){ unsigned long v=lng?va_arg(ap,unsigned long):(unsigned long)va_arg(ap,unsigned int); char nb[32]; int nn=qsh_utoa(v,10,0,nb); int zr=(prec>nn)?(prec-nn):0; tn=0; for(int i=0;i<zr;i++)tmp[tn++]='0'; for(int i=0;i<nn;i++)tmp[tn++]=nb[i]; tmp[tn]=0; }
        else if(c=='x'||c=='X'){ unsigned long v=lng?va_arg(ap,unsigned long):(unsigned long)va_arg(ap,unsigned int); char nb[32]; int nn=qsh_utoa(v,16,c=='X',nb); int zr=(prec>nn)?(prec-nn):0; tn=0; for(int i=0;i<zr;i++)tmp[tn++]='0'; for(int i=0;i<nn;i++)tmp[tn++]=nb[i]; tmp[tn]=0; }
        else if(c=='o'){ unsigned long v=lng?va_arg(ap,unsigned long):(unsigned long)va_arg(ap,unsigned int); tn=qsh_utoa(v,8,0,tmp); }
        else if(c=='p'){ unsigned long v=(unsigned long)va_arg(ap,void*); tmp[0]='0';tmp[1]='x'; tn=2+qsh_utoa(v,16,0,tmp+2); }
        else if(c=='c'){ tmp[0]=(char)va_arg(ap,int); tmp[1]=0; tn=1; }
        else if(c=='s'){ src=va_arg(ap,const char*); if(!src)src="(null)"; tn=0; while(src[tn]&&(prec<0||tn<prec))tn++; }
        else if(c=='f'||c=='g'||c=='e'){ double d=va_arg(ap,double); int neg=d<0; if(neg)d=-d; long ip=(long)d; double fr=d-(double)ip; int pd=(prec<0)?6:prec; char nb[32]; int nn=qsh_utoa((unsigned long)ip,10,0,nb); tn=0; if(neg)tmp[tn++]='-'; for(int i=0;i<nn;i++)tmp[tn++]=nb[i]; if(pd>0){ tmp[tn++]='.'; for(int k=0;k<pd&&tn<46;k++){ fr*=10; int dg=(int)fr; if(dg<0)dg=0; if(dg>9)dg=9; tmp[tn++]='0'+dg; fr-=dg; } } tmp[tn]=0; }
        else if(c=='%'){ tmp[0]='%'; tn=1; }
        else { tmp[0]=c; tn=1; }
        int padn=width-tn; if(padn<0)padn=0;
        if(!left) for(int i=0;i<padn;i++)PUT(pad);
        for(int i=0;i<tn;i++)PUT(src[i]);
        if(left) for(int i=0;i<padn;i++)PUT(' ');
    }
    if(cap>0){ if(o<cap)buf[o]=0; else buf[cap-1]=0; }
    return (int)o;
    #undef PUT
}
int snprintf(char* buf,size_t cap,const char* fmt,...){ va_list ap; va_start(ap,fmt); int r=vsnprintf(buf,cap,fmt,ap); va_end(ap); return r; }
int sprintf(char* buf,const char* fmt,...){ va_list ap; va_start(ap,fmt); int r=vsnprintf(buf,1<<20,fmt,ap); va_end(ap); return r; }
int vprintf(const char* fmt,va_list ap){ char b[2048]; int r=vsnprintf(b,sizeof(b),fmt,ap); qsh_emit(b,r>(int)sizeof(b)-1?(int)sizeof(b)-1:r); return r; }
int printf(const char* fmt,...){ va_list ap; va_start(ap,fmt); int r=vprintf(fmt,ap); va_end(ap); return r; }
int puts(const char* s){ int n=(int)strlen(s); qsh_emit(s,n); qsh_emit("\n",1); return n+1; }
int putchar(int c){ char b=(char)c; qsh_emit(&b,1); return c; }
int getchar(void){ return -1; }


int sscanf(const char* s,const char* fmt,...){ va_list ap; va_start(ap,fmt); int got=0;
    for(const char* p=fmt;*p;p++){ if(*p==' '){ while(*s==' '||*s=='\t'||*s=='\n')s++; continue; }
        if(*p!='%'){ if(*s==*p)s++; else break; continue; } p++;
        while(*p=='l'||*p=='h')p++; char c=*p;
        while(*s==' '||*s=='\t'||*s=='\n')s++;
        if(c=='d'||c=='i'||c=='u'){ int sgn=1,v=0,any=0; if(*s=='-'){sgn=-1;s++;} else if(*s=='+')s++; while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;any=1;} if(!any)break; *va_arg(ap,int*)=sgn*v; got++; }
        else if(c=='x'||c=='X'){ unsigned v=0,any=0; while(1){ int d; char h=*s; if(h>='0'&&h<='9')d=h-'0'; else if(h>='a'&&h<='f')d=h-'a'+10; else if(h>='A'&&h<='F')d=h-'A'+10; else break; v=v*16+d; s++; any=1; } if(!any)break; *va_arg(ap,unsigned*)=v; got++; }
        else if(c=='f'||c=='g'||c=='e'){ char nb[64]; int n=0; if(*s=='-'||*s=='+')nb[n++]=*s++; while((*s>='0'&&*s<='9')||*s=='.'){ if(n<63)nb[n++]=*s; s++; } nb[n]=0; if(!n)break; *va_arg(ap,float*)=(float)atof(nb); got++; }
        else if(c=='s'){ char* out=va_arg(ap,char*); int n=0; while(*s&&*s!=' '&&*s!='\t'&&*s!='\n'){ out[n++]=*s++; } out[n]=0; if(!n)break; got++; }
        else if(c=='c'){ *va_arg(ap,char*)=*s++; got++; }
        else if(c=='%'){ if(*s=='%')s++; }
    }
    va_end(ap); return got;
}


struct _DFILE { unsigned char* data; long len; long pos; int writable; };
struct _DFILE qsh_stdout={0,0,0,1}, qsh_stderr={0,0,0,1}, qsh_stdin={0,0,0,0};
void* stdout=(void*)&qsh_stdout; void* stderr=(void*)&qsh_stderr; void* stdin=(void*)&qsh_stdin;
#define QSH_MAXF 8
static struct { const char* name; unsigned char* buf; long len; } qsh_files[QSH_MAXF]; static int qsh_nf=0;
void qsh_set_file(const char* name,unsigned char* buf,long len){ if(qsh_nf<QSH_MAXF){ qsh_files[qsh_nf].name=name; qsh_files[qsh_nf].buf=buf; qsh_files[qsh_nf].len=len; qsh_nf++; } }
static int qsh_name_eq(const char* a,const char* b){  const char* ba=a; for(const char* q=a;*q;q++)if(*q=='/'||*q=='\\')ba=q+1; const char* bb=b; for(const char* q=b;*q;q++)if(*q=='/'||*q=='\\')bb=q+1; return strcasecmp(ba,bb)==0; }
typedef struct _DFILE FILE;
FILE* fopen(const char* path,const char* mode){ int wr=(mode&&(mode[0]=='w'||mode[0]=='a')); 
    if(!wr){ for(int i=0;i<qsh_nf;i++) if(qsh_name_eq(path,qsh_files[i].name)){ FILE* f=(FILE*)malloc(sizeof(FILE)); if(!f)return 0; f->data=qsh_files[i].buf; f->len=qsh_files[i].len; f->pos=0; f->writable=0; return f; } return 0; }
    FILE* f=(FILE*)malloc(sizeof(FILE)); if(!f)return 0; f->data=0; f->len=0; f->pos=0; f->writable=1; return f;   
}
int fclose(FILE* f){ if(f&&f!=(FILE*)&qsh_stdout&&f!=(FILE*)&qsh_stderr&&f!=(FILE*)&qsh_stdin)free(f); return 0; }
size_t fread(void* ptr,size_t sz,size_t n,FILE* f){ if(!f||!f->data)return 0; long want=(long)sz*(long)n; long avail=f->len-f->pos; if(want>avail)want=avail; if(want<0)want=0; unsigned char* d=(unsigned char*)ptr; for(long i=0;i<want;i++)d[i]=f->data[f->pos+i]; f->pos+=want; return sz?(size_t)(want/(long)sz):0; }
size_t fwrite(const void* ptr,size_t sz,size_t n,FILE* f){ if(f==(FILE*)&qsh_stdout||f==(FILE*)&qsh_stderr){ qsh_emit((const char*)ptr,(int)(sz*n)); } return n; }
int fseek(FILE* f,long off,int whence){ if(!f)return -1; if(whence==0)f->pos=off; else if(whence==1)f->pos+=off; else f->pos=f->len+off; if(f->pos<0)f->pos=0; if(f->pos>f->len)f->pos=f->len; return 0; }
long ftell(FILE* f){ return f?f->pos:-1; }
int fflush(FILE* f){ (void)f; return 0; }
int feof(FILE* f){ return f?(f->pos>=f->len):1; }
int fgetc(FILE* f){ if(!f||!f->data||f->pos>=f->len)return -1; return f->data[f->pos++]; }
int fputc(int c,FILE* f){ if(f==(FILE*)&qsh_stdout||f==(FILE*)&qsh_stderr){ char b=(char)c; qsh_emit(&b,1); } return c; }
int ungetc(int c,FILE* f){ if(f&&f->pos>0){ f->pos--; } return c; }
char* fgets(char* buf,int n,FILE* f){ if(!f||!f->data)return 0; int i=0; while(i<n-1&&f->pos<f->len){ char c=f->data[f->pos++]; buf[i++]=c; if(c=='\n')break; } buf[i]=0; return i?buf:0; }
int fputs(const char* s,FILE* f){ if(f==(FILE*)&qsh_stdout||f==(FILE*)&qsh_stderr)qsh_emit(s,(int)strlen(s)); return 0; }
int fprintf(FILE* f,const char* fmt,...){ char b[2048]; va_list ap; va_start(ap,fmt); int r=vsnprintf(b,sizeof(b),fmt,ap); va_end(ap); if(f==(FILE*)&qsh_stdout||f==(FILE*)&qsh_stderr)qsh_emit(b,r>(int)sizeof(b)-1?(int)sizeof(b)-1:r); return r; }
int vfprintf(FILE* f,const char* fmt,va_list ap){ char b[2048]; int r=vsnprintf(b,sizeof(b),fmt,ap); if(f==(FILE*)&qsh_stdout||f==(FILE*)&qsh_stderr)qsh_emit(b,r>(int)sizeof(b)-1?(int)sizeof(b)-1:r); return r; }
int remove(const char* p){ (void)p; return 0; }
int rename(const char* a,const char* b){ (void)a;(void)b; return 0; }


int open(const char* p,int fl,...){ (void)p;(void)fl; return -1; }
int close(int fd){ (void)fd; return 0; }
long lseek(int fd,long o,int w){ (void)fd;(void)o;(void)w; return -1; }
long read(int fd,void* b,unsigned long n){ (void)fd;(void)b;(void)n; return -1; }
long write(int fd,const void* b,unsigned long n){ (void)fd; if(fd==1||fd==2)qsh_emit((const char*)b,(int)n); return (long)n; }
int access(const char* p,int m){ (void)m; for(int i=0;i<qsh_nf;i++) if(qsh_name_eq(p,qsh_files[i].name))return 0; return -1; }
int unlink(const char* p){ (void)p; return 0; }
int mkdir(const char* p,int m){ (void)p;(void)m; return 0; }
struct stat; int stat(const char* p,struct stat* st){ (void)p;(void)st; return -1; }
struct timeval; int gettimeofday(struct timeval* tv,void* tz){ (void)tv;(void)tz; return 0; }


char* getenv(const char* n){ (void)n; return 0; }
int system(const char* c){ (void)c; return -1; }
void qsort(void* base,size_t n,size_t sz,int(*cmp)(const void*,const void*)){ 
    unsigned char* a=(unsigned char*)base; unsigned char tmp[256]; if(sz>256)return;
    for(size_t i=1;i<n;i++){ for(size_t j=i;j>0 && cmp(a+(j-1)*sz,a+j*sz)>0;j--){ for(size_t b=0;b<sz;b++)tmp[b]=a[(j-1)*sz+b]; for(size_t b=0;b<sz;b++)a[(j-1)*sz+b]=a[j*sz+b]; for(size_t b=0;b<sz;b++)a[j*sz+b]=tmp[b]; } } }
int errno=0;
static void (*qsh_exit_hook)(int)=0;
void qsh_set_exit(void (*fn)(int)){ qsh_exit_hook=fn; }
void abort(void){  if(qsh_exit_hook)qsh_exit_hook(134); for(;;){} }
void exit(int code){  if(qsh_exit_hook)qsh_exit_hook(code); for(;;){} }

char* strerror(int e){ (void)e; return "error"; }
int fscanf(FILE* f,const char* fmt,...){ (void)f; (void)fmt; return 0; }
long time(long* t){ if(t)*t=0; return 0; }
static unsigned long long (*qsh_clock)(void)=0;
void qsh_set_clock(unsigned long long (*fn)(void)){ qsh_clock=fn; }
double qsh_time(void){ return qsh_clock?(double)qsh_clock()/1e9:0.0; }
extern void QG_Tick(double);
void qsh_tick_us(unsigned long us){ QG_Tick((double)us/1e6); }
__asm__(".globl setjmp\nsetjmp:\n mov %rbx,0(%rdi)\n mov %rbp,8(%rdi)\n mov %r12,16(%rdi)\n mov %r13,24(%rdi)\n mov %r14,32(%rdi)\n mov %r15,40(%rdi)\n lea 8(%rsp),%rdx\n mov %rdx,48(%rdi)\n mov (%rsp),%rdx\n mov %rdx,56(%rdi)\n xor %eax,%eax\n ret\n"
        ".globl longjmp\nlongjmp:\n mov %esi,%eax\n test %eax,%eax\n jnz 1f\n inc %eax\n1:\n mov 0(%rdi),%rbx\n mov 8(%rdi),%rbp\n mov 16(%rdi),%r12\n mov 24(%rdi),%r13\n mov 32(%rdi),%r14\n mov 40(%rdi),%r15\n mov 48(%rdi),%rsp\n jmp *56(%rdi)\n");
int getc(FILE* f){ return fgetc(f); }
static unsigned long qsh_seed=1;
int rand(void){ qsh_seed=qsh_seed*1103515245UL+12345UL; return (int)((qsh_seed>>16)&0x7FFF); }
void srand(unsigned s){ qsh_seed=s; }
int vsprintf(char* b,const char* f,va_list ap){ return vsnprintf(b,1<<20,f,ap); }
