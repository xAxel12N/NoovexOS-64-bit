#include <nvx/_types.h>
int fgetc(FILE*); int ungetc(int,FILE*); int isspace(int); extern FILE __stdin;
double strtod(const char*,char**); unsigned long long strtoull(const char*,char**,int); long long strtoll(const char*,char**,int);
size_t strlen(const char*);
static const char* __WD[7]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
static const char* __MN[12]={"January","February","March","April","May","June","July","August","September","October","November","December"};
static int __isleapy(int y){ return (y%4==0&&y%100!=0)||y%400==0; }
static int __isowk(const struct tm* t,int* year){ int y=t->tm_year+1900; int wd=(t->tm_wday+6)%7; int w=(t->tm_yday-wd+10)/7;
    if(w<1){ y--; int pyd=t->tm_yday+(__isleapy(y)?366:365); w=(pyd-wd+10)/7; }
    else if(w==53){ int dy=__isleapy(y)?366:365; if(t->tm_yday-wd+3>=dy){ w=1; y++; } }
    if(year)*year=y; return w; }
static long long __tm2epoch(const struct tm* t){ long long y=t->tm_year+1900LL, m=t->tm_mon+1; if(m<=2){ y--; m+=12; } long long days=365*y+y/4-y/100+y/400+(153*(m-3)+2)/5+t->tm_mday-719469; return ((days*24+t->tm_hour)*60+t->tm_min)*60+t->tm_sec; }
size_t strftime(char* s,size_t max,const char* f,const struct tm* t){
    size_t n=0; char tmp[64];
#define PUTS(str) do{ const char* _z=(str); while(*_z){ if(n+1>=max)return 0; s[n++]=*_z++; } }while(0)
#define PUTN(v,w,padc) do{ long long _v=(v); int _neg=_v<0; if(_neg)_v=-_v; char _b[24]; int _k=0; do{ _b[_k++]=(char)('0'+_v%10); _v/=10; }while(_v); while(_k<(w))_b[_k++]=(padc); if(_neg)_b[_k++]='-'; while(_k){ if(n+1>=max)return 0; s[n++]=_b[--_k]; } }while(0)
    for(;*f;f++){
        if(*f!='%'){ if(n+1>=max)return 0; s[n++]=*f; continue; }
        f++; if(*f=='E'||*f=='O')f++;
        switch(*f){
        case 'a': { char b[4]={__WD[t->tm_wday%7][0],__WD[t->tm_wday%7][1],__WD[t->tm_wday%7][2],0}; PUTS(b); } break;
        case 'A': PUTS(__WD[t->tm_wday%7]); break;
        case 'b': case 'h': { char b[4]={__MN[t->tm_mon%12][0],__MN[t->tm_mon%12][1],__MN[t->tm_mon%12][2],0}; PUTS(b); } break;
        case 'B': PUTS(__MN[t->tm_mon%12]); break;
        case 'c': { size_t r=strftime(tmp,sizeof tmp,"%a %b %e %H:%M:%S %Y",t); (void)r; PUTS(tmp); } break;
        case 'C': PUTN((t->tm_year+1900)/100,2,'0'); break;
        case 'd': PUTN(t->tm_mday,2,'0'); break;
        case 'D': strftime(tmp,sizeof tmp,"%m/%d/%y",t); PUTS(tmp); break;
        case 'e': PUTN(t->tm_mday,2,' '); break;
        case 'F': strftime(tmp,sizeof tmp,"%Y-%m-%d",t); PUTS(tmp); break;
        case 'g': { int y; __isowk(t,&y); PUTN(y%100,2,'0'); } break;
        case 'G': { int y; __isowk(t,&y); PUTN(y,1,'0'); } break;
        case 'H': PUTN(t->tm_hour,2,'0'); break;
        case 'I': PUTN(t->tm_hour%12?t->tm_hour%12:12,2,'0'); break;
        case 'j': PUTN(t->tm_yday+1,3,'0'); break;
        case 'k': PUTN(t->tm_hour,2,' '); break;
        case 'l': PUTN(t->tm_hour%12?t->tm_hour%12:12,2,' '); break;
        case 'm': PUTN(t->tm_mon+1,2,'0'); break;
        case 'M': PUTN(t->tm_min,2,'0'); break;
        case 'n': PUTS("\n"); break;
        case 'p': PUTS(t->tm_hour<12?"AM":"PM"); break;
        case 'P': PUTS(t->tm_hour<12?"am":"pm"); break;
        case 'r': strftime(tmp,sizeof tmp,"%I:%M:%S %p",t); PUTS(tmp); break;
        case 'R': strftime(tmp,sizeof tmp,"%H:%M",t); PUTS(tmp); break;
        case 's': PUTN(__tm2epoch(t),1,'0'); break;
        case 'S': PUTN(t->tm_sec,2,'0'); break;
        case 't': PUTS("\t"); break;
        case 'T': strftime(tmp,sizeof tmp,"%H:%M:%S",t); PUTS(tmp); break;
        case 'u': PUTN(t->tm_wday?t->tm_wday:7,1,'0'); break;
        case 'U': PUTN((t->tm_yday+7-t->tm_wday)/7,2,'0'); break;
        case 'V': PUTN(__isowk(t,0),2,'0'); break;
        case 'w': PUTN(t->tm_wday,1,'0'); break;
        case 'W': PUTN((t->tm_yday+7-((t->tm_wday+6)%7))/7,2,'0'); break;
        case 'x': strftime(tmp,sizeof tmp,"%m/%d/%y",t); PUTS(tmp); break;
        case 'X': strftime(tmp,sizeof tmp,"%H:%M:%S",t); PUTS(tmp); break;
        case 'y': PUTN((t->tm_year+1900)%100,2,'0'); break;
        case 'Y': PUTN(t->tm_year+1900,1,'0'); break;
        case 'z': PUTS("+0000"); break;
        case 'Z': PUTS("GMT"); break;
        case '%': PUTS("%"); break;
        case 0: f--; break;
        default: { char b[3]={'%',*f,0}; PUTS(b); } }
    }
    if(n<max)s[n]=0; return n;
#undef PUTS
#undef PUTN
}
typedef struct { int (*get)(void*); void (*unget)(void*,int); void* ctx; int count; } __rd;
static int __sget(void* c){ const char** p=(const char**)c; if(!**p)return -1; return (unsigned char)*(*p)++; }
static void __sunget(void* c,int ch){ const char** p=(const char**)c; if(ch>=0)(*p)--; }
static int __fget(void* c){ return fgetc((FILE*)c); }
static void __funget(void* c,int ch){ if(ch>=0)ungetc(ch,(FILE*)c); }
static int __g(__rd* r){ int c=r->get(r->ctx); if(c>=0)r->count++; return c; }
static void __u(__rd* r,int c){ if(c>=0){ r->unget(r->ctx,c); r->count--; } }
static int __vscan(__rd* r,const char* f,va_list ap){
    int assigned=0, convs=0;
    for(;*f;f++){
        if(isspace((unsigned char)*f)){ int c; do c=__g(r); while(c>=0&&isspace(c)); __u(r,c); continue; }
        if(*f!='%'||f[1]=='%'){ if(*f=='%')f++; int c=__g(r); if(c!=(unsigned char)*f){ __u(r,c); if(c<0&&!convs)return -1; return assigned; } continue; }
        f++; int sup=0; if(*f=='*'){ sup=1; f++; }
        int width=0; while(*f>='0'&&*f<='9')width=width*10+(*f++-'0');
        int len=0; if(*f=='h'){ len=1; f++; if(*f=='h'){ len=0x10; f++; } } else if(*f=='l'){ len=2; f++; if(*f=='l'){ len=3; f++; } } else if(*f=='j'||*f=='z'||*f=='t'||*f=='q'){ len=3; f++; } else if(*f=='L'){ len=4; f++; }
        char cv=*f; if(!cv)break;
        if(cv=='n'){ if(!sup){ int* p=va_arg(ap,int*); *p=r->count; } continue; }
        int c;
        if(cv!='c'&&cv!='['){ do c=__g(r); while(c>=0&&isspace(c)); if(c<0)return convs?assigned:-1; __u(r,c); }
        if(cv=='c'){ int w=width?width:1; char* p=sup?0:va_arg(ap,char*); int got=0; for(;got<w;got++){ c=__g(r); if(c<0)break; if(p)p[got]=(char)c; } if(!got)return convs?assigned:-1; convs++; if(!sup)assigned++; continue; }
        if(cv=='s'||cv=='['){ char set[256]; int inv=0;
            if(cv=='['){ for(int i=0;i<256;i++)set[i]=0; f++; if(*f=='^'){ inv=1; f++; } if(*f==']'){ set[']']=1; f++; }
                while(*f&&*f!=']'){ if(f[1]=='-'&&f[2]&&f[2]!=']'){ for(int ch=(unsigned char)f[0];ch<=(unsigned char)f[2];ch++)set[ch]=1; f+=3; } else { set[(unsigned char)*f]=1; f++; } } }
            char* p=sup?0:va_arg(ap,char*); int got=0, w=width?width:0x7FFFFFFF;
            while(got<w){ c=__g(r); if(c<0)break; int ok=cv=='s'?!isspace(c):(inv?!set[c]:set[c]); if(!ok){ __u(r,c); break; } if(p)p[got]=(char)c; got++; }
            if(!got)return convs?assigned:(c<0?-1:assigned); if(p)p[got]=0; convs++; if(!sup)assigned++; continue; }
        char buf[512]; int bl=0, w=width?width:511; if(w>511)w=511;
        if(cv=='d'||cv=='i'||cv=='u'||cv=='o'||cv=='x'||cv=='X'||cv=='p'){
            int base=cv=='o'?8:(cv=='x'||cv=='X'||cv=='p')?16:cv=='i'?0:10;
            c=__g(r); if((c=='+'||c=='-')&&bl<w){ buf[bl++]=(char)c; c=__g(r); }
            if((base==0||base==16)&&c=='0'&&bl<w){ buf[bl++]='0'; c=__g(r); if((c=='x'||c=='X')&&bl<w){ buf[bl++]=(char)c; c=__g(r); if(base==0)base=16; } else if(base==0)base=8; }
            if(base==0)base=10;
            while(c>=0&&bl<w){ int dv=(c>='0'&&c<='9')?c-'0':(c>='a'&&c<='z')?c-'a'+10:(c>='A'&&c<='Z')?c-'A'+10:99; if(dv>=base)break; buf[bl++]=(char)c; c=__g(r); }
            __u(r,c); buf[bl]=0;
            int digits=0; for(int i=0;i<bl;i++){ char ch=buf[i]; if(ch!='+'&&ch!='-'&&!(i>0&&(ch=='x'||ch=='X')&&buf[i-1]=='0'))digits=1; }
            if(!digits)return assigned;
            convs++; if(sup)continue;
            if(cv=='p'){ *va_arg(ap,void**)=(void*)(uintptr_t)strtoull(buf,0,16); assigned++; continue; }
            if(cv=='d'||cv=='i'){ long long v=strtoll(buf,0,base==8&&cv=='i'?8:(base==16?16:base)); if(len==3)*va_arg(ap,long long*)=v; else if(len==2)*va_arg(ap,long*)=(long)v; else if(len==1)*va_arg(ap,short*)=(short)v; else if(len==0x10)*va_arg(ap,signed char*)=(signed char)v; else *va_arg(ap,int*)=(int)v; }
            else { unsigned long long v=strtoull(buf,0,base); if(len==3)*va_arg(ap,unsigned long long*)=v; else if(len==2)*va_arg(ap,unsigned long*)=(unsigned long)v; else if(len==1)*va_arg(ap,unsigned short*)=(unsigned short)v; else if(len==0x10)*va_arg(ap,unsigned char*)=(unsigned char)v; else *va_arg(ap,unsigned*)=(unsigned)v; }
            assigned++; continue; }
        if(cv=='f'||cv=='e'||cv=='g'||cv=='E'||cv=='G'||cv=='a'||cv=='A'||cv=='F'){
            c=__g(r);
            while(c>=0&&bl<w){ int ok=(c>='0'&&c<='9')||c=='.'||c=='e'||c=='E'||c=='+'||c=='-'||c=='x'||c=='X'||(c>='a'&&c<='f')||(c>='A'&&c<='F')||c=='p'||c=='P'||c=='n'||c=='N'||c=='i'||c=='I'||c=='t'||c=='T'||c=='y'||c=='Y';
                if(!ok)break; if((c=='+'||c=='-')&&bl>0&&buf[bl-1]!='e'&&buf[bl-1]!='E'&&buf[bl-1]!='p'&&buf[bl-1]!='P')break; buf[bl++]=(char)c; c=__g(r); }
            buf[bl]=0; char* e; double v=strtod(buf,&e); int used=(int)(e-buf);
            __u(r,c); for(int i=bl-1;i>=used;i--)__u(r,(unsigned char)buf[i]);
            if(!used)return assigned;
            convs++; if(sup)continue;
            if(len==2||len==4)*va_arg(ap,double*)=v; else *va_arg(ap,float*)=(float)v; assigned++; continue; }
        return assigned;
    }
    return assigned;
}
int vsscanf(const char* s,const char* f,va_list ap){ const char* p=s; __rd r={__sget,__sunget,&p,0}; return __vscan(&r,f,ap); }
int sscanf(const char* s,const char* f,...){ va_list ap; va_start(ap,f); int n=vsscanf(s,f,ap); va_end(ap); return n; }
int vfscanf(FILE* fp,const char* f,va_list ap){ __rd r={__fget,__funget,fp,0}; return __vscan(&r,f,ap); }
int fscanf(FILE* fp,const char* f,...){ va_list ap; va_start(ap,f); int n=vfscanf(fp,f,ap); va_end(ap); return n; }
int vscanf(const char* f,va_list ap){ return vfscanf(&__stdin,f,ap); }
int scanf(const char* f,...){ va_list ap; va_start(ap,f); int n=vfscanf(&__stdin,f,ap); va_end(ap); return n; }
