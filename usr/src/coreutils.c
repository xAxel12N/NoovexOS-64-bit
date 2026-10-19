#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <nvx/extras.h>
static int cu_rc;
static const char* cu_me;
static void cu_err(const char* what,const char* why){ fprintf(stderr,"%s: %s: %s\n",cu_me,what,why); cu_rc=2; }
typedef struct { char** v; size_t n, cap; } lines_t;
static void lpush(lines_t* L,char* s){ if(L->n==L->cap){ L->cap=L->cap?L->cap*2:64; L->v=realloc(L->v,L->cap*sizeof(char*)); } L->v[L->n++]=s; }
static FILE* openin(const char* p){ if(!strcmp(p,"-"))return stdin; FILE* f=fopen(p,"r"); if(!f)cu_err(p,"No such file or directory"); return f; }
static void readall(FILE* f,lines_t* L,int* lastnl){ char* b=0; size_t c=0; long n; if(lastnl)*lastnl=1;
    while((n=getline(&b,&c,f))>0){ int nl=b[n-1]=='\n'; if(nl)b[--n]=0; if(lastnl)*lastnl=nl; lpush(L,strdup(b)); } free(b); }
static int nfiles(int argc,char** argv,int first){ int n=0; for(int i=first;i<argc;i++) if(argv[i][0]!='-'||!argv[i][1])n++; return n; }
static int numarg(const char* s){ return atoi(s[0]=='+'||s[0]=='-'?s+1:s); }
static int cu_head(int argc,char** argv){ long n=10; int i=1; int bytes=0;
    for(;i<argc&&argv[i][0]=='-'&&argv[i][1];i++){ if(!strcmp(argv[i],"-n")&&i+1<argc)n=atol(argv[++i]); else if(!strcmp(argv[i],"-c")&&i+1<argc){ n=atol(argv[++i]); bytes=1; } else if(argv[i][1]=='n')n=atol(argv[i]+2); else if(argv[i][1]>='0'&&argv[i][1]<='9')n=atol(argv[i]+1); }
    int nf=argc-i; if(nf==0){ argv[argc]="-"; argc++; nf=1; }
    for(int k=i;k<argc;k++){ FILE* f=openin(argv[k]); if(!f)continue; if(nf>1)printf("%s==> %s <==\n",k>i?"\n":"",strcmp(argv[k],"-")?argv[k]:"standard input");
        if(bytes){ int c; for(long m=0;m<n&&(c=fgetc(f))!=EOF;m++)putchar(c); }
        else { char* b=0; size_t c=0; long r; for(long m=0;m<n&&(r=getline(&b,&c,f))>0;m++)fwrite(b,1,(size_t)r,stdout); free(b); }
        if(f!=stdin)fclose(f); }
    return cu_rc; }
static int cu_tail(int argc,char** argv){ long n=10; int i=1, from=0;
    for(;i<argc&&argv[i][0]=='-'&&argv[i][1];i++){ const char* a=0; if(!strcmp(argv[i],"-n")&&i+1<argc)a=argv[++i]; else if(argv[i][1]=='n')a=argv[i]+2; else if(argv[i][1]>='0'&&argv[i][1]<='9')a=argv[i]+1; if(a){ if(a[0]=='+')from=1; n=numarg(a); } }
    if(i==argc){ argv[argc]="-"; argc++; }
    int nf=argc-i;
    for(int k=i;k<argc;k++){ FILE* f=openin(argv[k]); if(!f)continue; if(nf>1)printf("%s==> %s <==\n",k>i?"\n":"",argv[k]);
        lines_t L={0}; int lastnl; readall(f,&L,&lastnl); if(f!=stdin)fclose(f);
        size_t s=from?(n>0?(size_t)(n-1):0):(L.n>(size_t)n?L.n-(size_t)n:0);
        for(size_t j=s;j<L.n;j++){ fputs(L.v[j],stdout); if(j+1<L.n||lastnl)putchar('\n'); }
        for(size_t j=0;j<L.n;j++)free(L.v[j]); free(L.v); }
    return cu_rc; }
static int wdig(long v){ int d=1; while(v>=10){ v/=10; d++; } return d; }
static int cu_wc(int argc,char** argv){ int l=0,w=0,c=0,i=1;
    for(;i<argc&&argv[i][0]=='-'&&argv[i][1];i++) for(int k=1;argv[i][k];k++){ if(argv[i][k]=='l')l=1; else if(argv[i][k]=='w')w=1; else if(argv[i][k]=='c')c=1; }
    if(!l&&!w&&!c)l=w=c=1;
    int nf=argc-i; int cols=l+w+c; long tl=0,tw=0,tc=0; long totsize=0; int nonreg=0;
    if(nf==0){ argv[argc]="-"; argc++; nf=1; }
    for(int k=i;k<argc;k++){ struct stat st; if(!strcmp(argv[k],"-")){ nonreg=1; } else if(stat(argv[k],&st)==0)totsize+=(long)st.st_size; }
    int width=wdig(totsize); if(nonreg&&width<7)width=7; if(cols==1&&nf==1)width=1;
    long R[64][3]; char ok[64];
    for(int k=i;k<argc&&k-i<64;k++){ FILE* f=openin(argv[k]); ok[k-i]=f!=0; long a=0,b=0,d=0; if(f){ int ch, inw=0; while((ch=fgetc(f))!=EOF){ d++; if(ch=='\n')a++; if(ch==' '||(ch>='\t'&&ch<='\r'))inw=0; else if(!inw){ inw=1; b++; } } if(f!=stdin)fclose(f); }
        R[k-i][0]=a; R[k-i][1]=b; R[k-i][2]=d; tl+=a; tw+=b; tc+=d; }
    for(int k=i;k<argc&&k-i<64;k++){ if(!ok[k-i])continue; int first=1;
        if(l){ printf("%*ld",width,R[k-i][0]); first=0; } if(w){ printf(first?"%*ld":" %*ld",width,R[k-i][1]); first=0; } if(c){ printf(first?"%*ld":" %*ld",width,R[k-i][2]); }
        if(strcmp(argv[k],"-"))printf(" %s",argv[k]); putchar('\n'); }
    if(nf>1){ int first=1; if(l){ printf("%*ld",width,tl); first=0; } if(w){ printf(first?"%*ld":" %*ld",width,tw); first=0; } if(c)printf(first?"%*ld":" %*ld",width,tc); printf(" total\n"); }
    return cu_rc?1:0; }
static int s_num, s_rev, s_fold;
static double lead_num(const char* s){ char* e; double v=strtod(s,&e); if(e==s)return 0; return v; }
static int s_cmp(const void* a,const void* b){ const char* x=*(char* const*)a; const char* y=*(char* const*)b; int r;
    if(s_num){ double p=lead_num(x), q=lead_num(y); r=(p>q)-(p<q); if(!r)r=strcmp(x,y); }
    else if(s_fold){ r=strcasecmp(x,y); if(!r)r=strcmp(x,y); } else r=strcmp(x,y);
    return s_rev?-r:r; }
static int cu_sort(int argc,char** argv){ int i=1, uniq=0; s_num=s_rev=s_fold=0;
    for(;i<argc&&argv[i][0]=='-'&&argv[i][1];i++) for(int k=1;argv[i][k];k++){ char o=argv[i][k]; if(o=='n')s_num=1; else if(o=='r')s_rev=1; else if(o=='u')uniq=1; else if(o=='f')s_fold=1; }
    lines_t L={0}; if(i==argc)readall(stdin,&L,0);
    for(;i<argc;i++){ FILE* f=openin(argv[i]); if(!f)return 2; readall(f,&L,0); if(f!=stdin)fclose(f); }
    qsort(L.v,L.n,sizeof(char*),s_cmp);
    for(size_t j=0;j<L.n;j++){ if(uniq&&j>0&&s_cmp(&L.v[j],&L.v[j-1])==0)continue; puts(L.v[j]); }
    return 0; }
static const char* ci_strstr(const char* h,const char* n){ size_t nl=strlen(n); for(;*h;h++) if(!strncasecmp(h,n,nl))return h; return nl?0:h; }
static int cu_grep(int argc,char** argv){ int i=1,inv=0,ic=0,cnt=0,num=0,lst=0,q=0;
    for(;i<argc&&argv[i][0]=='-'&&argv[i][1];i++) for(int k=1;argv[i][k];k++){ char o=argv[i][k]; if(o=='v')inv=1; else if(o=='i')ic=1; else if(o=='c')cnt=1; else if(o=='n')num=1; else if(o=='l')lst=1; else if(o=='q')q=1; }
    if(i>=argc){ fprintf(stderr,"usage: grep [-ivcnlq] PATTERN [FILE...]\n"); return 2; }
    const char* pat=argv[i++]; int nf=argc-i; int any=0, errs=0;
    if(nf==0){ argv[argc]="-"; argc++; nf=1; }
    for(;i<argc;i++){ FILE* f=strcmp(argv[i],"-")?fopen(argv[i],"r"):stdin; if(!f){ fprintf(stderr,"grep: %s: No such file or directory\n",argv[i]); errs=1; continue; }
        char* b=0; size_t c=0; long n; long ln=0, hits=0;
        while((n=getline(&b,&c,f))>0){ ln++; if(b[n-1]=='\n')b[n-1]=0; int m=(ic?ci_strstr(b,pat):strstr(b,pat))!=0; if(m==inv)continue; hits++; any=1;
            if(q){ free(b); if(f!=stdin)fclose(f); return 0; } if(lst||cnt)continue;
            if(nf>1)printf("%s:",argv[i]); if(num)printf("%ld:",ln); puts(b); }
        free(b); if(f!=stdin)fclose(f);
        if(cnt){ if(nf>1)printf("%s:",argv[i]); printf("%ld\n",hits); } if(lst&&hits)puts(argv[i]); }
    return errs?2:(any?0:1); }
static int cu_cut(int argc,char** argv){ char d='\t'; const char* list=0; int bych=0, i=1, only=0;
    for(;i<argc&&argv[i][0]=='-'&&argv[i][1];i++){ if(argv[i][1]=='d'){ d=argv[i][2]?argv[i][2]:(i+1<argc?argv[++i][0]:'\t'); } else if(argv[i][1]=='f'){ list=argv[i][2]?argv[i]+2:argv[++i]; } else if(argv[i][1]=='c'){ list=argv[i][2]?argv[i]+2:argv[++i]; bych=1; } else if(argv[i][1]=='s')only=1; }
    if(!list){ fprintf(stderr,"cut: you must specify a list of bytes, characters, or fields\n"); return 1; }
    char sel[1025]={0}; int open_end=0; { const char* p=list; while(*p){ int a=0,b=0; if(*p=='-')a=1; else while(*p>='0'&&*p<='9')a=a*10+(*p++-'0'); if(*p=='-'){ p++; if(*p>='0'&&*p<='9'){ while(*p>='0'&&*p<='9')b=b*10+(*p++-'0'); } else { b=1024; open_end=a; } } else b=a; for(int k=a;k<=b&&k<=1024;k++)if(k>0)sel[k]=1; if(*p==',')p++; else if(*p)p++; } }
    if(i==argc){ argv[argc]="-"; argc++; }
    for(;i<argc;i++){ FILE* f=openin(argv[i]); if(!f)continue; char* b=0; size_t c=0; long n;
        while((n=getline(&b,&c,f))>0){ if(b[n-1]=='\n')b[--n]=0;
            if(bych){ for(long k=0;k<n;k++) if(k+1<=1024&&sel[k+1])putchar(b[k]); else if(k+1>1024&&open_end)putchar(b[k]); putchar('\n'); continue; }
            if(!strchr(b,d)){ if(!only)puts(b); continue; }
            int fld=1, first=1; char* s=b; for(;;){ char* e=strchr(s,d); if(e)*e=0; if((fld<=1024&&sel[fld])||(fld>1024&&open_end)){ if(!first)putchar(d); fputs(s,stdout); first=0; } if(!e)break; s=e+1; fld++; } putchar('\n'); }
        free(b); if(f!=stdin)fclose(f); }
    return cu_rc?1:0; }
static int tr_expand(const char* s,unsigned char* out){ int n=0;
    while(*s&&n<1024){ if(s[0]=='['&&s[1]==':'){ const char* e=strstr(s,":]"); if(e){ char cl[16]={0}; memcpy(cl,s+2,(size_t)(e-s-2)<15?(size_t)(e-s-2):15);
                for(int ch=0;ch<256;ch++){ int m=!strcmp(cl,"upper")?(ch>='A'&&ch<='Z'):!strcmp(cl,"lower")?(ch>='a'&&ch<='z'):!strcmp(cl,"digit")?(ch>='0'&&ch<='9'):!strcmp(cl,"alpha")?((ch|32)>='a'&&(ch|32)<='z'):!strcmp(cl,"alnum")?(((ch|32)>='a'&&(ch|32)<='z')||(ch>='0'&&ch<='9')):!strcmp(cl,"space")?(ch==' '||(ch>='\t'&&ch<='\r')):!strcmp(cl,"punct")?(ch>32&&ch<127&&!(((ch|32)>='a'&&(ch|32)<='z')||(ch>='0'&&ch<='9'))):0; if(m)out[n++]=(unsigned char)ch; }
                s=e+2; continue; } }
        int c=(unsigned char)*s; if(c=='\\'&&s[1]){ s++; c=*s=='n'?'\n':*s=='t'?'\t':*s=='\\'?'\\':(unsigned char)*s; }
        if(s[1]=='-'&&s[2]){ int e2=(unsigned char)s[2]; for(int k=c;k<=e2&&n<1024;k++)out[n++]=(unsigned char)k; s+=3; continue; }
        out[n++]=(unsigned char)c; s++; }
    return n; }
static int cu_tr(int argc,char** argv){ int del=0,sq=0,i=1; for(;i<argc&&argv[i][0]=='-'&&argv[i][1];i++) for(int k=1;argv[i][k];k++){ if(argv[i][k]=='d')del=1; else if(argv[i][k]=='s')sq=1; }
    if(i>=argc){ fprintf(stderr,"tr: missing operand\n"); return 1; }
    unsigned char a[1024],b[1024]; int na=tr_expand(argv[i],a), nb=i+1<argc?tr_expand(argv[i+1],b):0;
    int map[256], inset[256]={0}, sqset[256]={0}; for(int k=0;k<256;k++)map[k]=k; for(int k=0;k<na;k++)inset[a[k]]=1;
    if(!del&&nb){ for(int k=0;k<na;k++)map[a[k]]=b[k<nb?k:nb-1]; for(int k=0;k<nb;k++)sqset[b[k]]=1; } else if(sq&&!del) for(int k=0;k<na;k++)sqset[a[k]]=1;
    if(del&&sq&&nb) for(int k=0;k<nb;k++)sqset[b[k]]=1;
    int ch, last=-1; while((ch=getchar())!=EOF){ if(del&&inset[ch])continue; int o=map[ch]; if(sq&&sqset[o]&&o==last)continue; putchar(o); last=o; }
    return 0; }
static int cu_uniq(int argc,char** argv){ int c=0,d=0,u=0,i=1; for(;i<argc&&argv[i][0]=='-'&&argv[i][1];i++) for(int k=1;argv[i][k];k++){ if(argv[i][k]=='c')c=1; else if(argv[i][k]=='d')d=1; else if(argv[i][k]=='u')u=1; }
    FILE* f=i<argc?openin(argv[i]):stdin; if(!f)return 1; lines_t L={0}; readall(f,&L,0);
    for(size_t j=0;j<L.n;){ size_t k=j; while(k<L.n&&!strcmp(L.v[k],L.v[j]))k++; long cnt=(long)(k-j);
        if((!d||cnt>1)&&(!u||cnt==1)){ if(c)printf("%7ld ",cnt); puts(L.v[j]); } j=k; }
    return 0; }
static int cu_seq(int argc,char** argv){ int i=1; const char* sep="\n"; int eqw=0;
    for(;i<argc&&argv[i][0]=='-'&&(argv[i][1]<'0'||argv[i][1]>'9');i++){ if(!strcmp(argv[i],"-s")&&i+1<argc)sep=argv[++i]; else if(argv[i][1]=='s'&&argv[i][2])sep=argv[i]+2; else if(!strcmp(argv[i],"-w"))eqw=1; }
    int n=argc-i; double a=1,s=1,b; if(n==1)b=atof(argv[i]); else if(n==2){ a=atof(argv[i]); b=atof(argv[i+1]); } else if(n==3){ a=atof(argv[i]); s=atof(argv[i+1]); b=atof(argv[i+2]); } else { fprintf(stderr,"seq: missing operand\n"); return 1; }
    int isint=1; for(int k=i;k<argc;k++) if(strchr(argv[k],'.')||strchr(argv[k],'e'))isint=0;
    int w=0; if(eqw&&isint){ char t[32]; w=sprintf(t,"%ld",(long)a); int w2=sprintf(t,"%ld",(long)b); if(w2>w)w=w2; }
    int first=1; for(long k=0;;k++){ double v=a+(double)k*s; if(s>0?v>b:v<b)break; if(!first)fputs(sep,stdout); first=0; if(isint)printf(eqw?"%0*ld":"%*ld",eqw?w:0,(long)v); else printf("%g",v); }
    if(!first)putchar('\n'); return 0; }
static int cu_basename(int argc,char** argv){ if(argc<2){ fprintf(stderr,"basename: missing operand\n"); return 1; } char p[4096]; strncpy(p,argv[1],4095); p[4095]=0;
    size_t n=strlen(p); while(n>1&&p[n-1]=='/')p[--n]=0; char* b=strrchr(p,'/'); b=(b&&b[1])?b+1:(b&&n==1?b:p);
    if(argc>2){ size_t bl=strlen(b), sl=strlen(argv[2]); if(sl<bl&&!strcmp(b+bl-sl,argv[2]))b[bl-sl]=0; } puts(b); return 0; }
static int cu_dirname(int argc,char** argv){ if(argc<2){ fprintf(stderr,"dirname: missing operand\n"); return 1; } char p[4096]; strncpy(p,argv[1],4095); p[4095]=0;
    size_t n=strlen(p); while(n>1&&p[n-1]=='/')p[--n]=0; char* s=strrchr(p,'/'); if(!s){ puts("."); return 0; } while(s>p&&*s=='/')s--; if(s==p&&*p=='/'){ puts("/"); return 0; } s[1]=0; puts(p); return 0; }
static int cu_rev(int argc,char** argv){ FILE* f=argc>1?openin(argv[1]):stdin; if(!f)return 1; char* b=0; size_t c=0; long n; while((n=getline(&b,&c,f))>0){ int nl=b[n-1]=='\n'; if(nl)n--; for(long k=n-1;k>=0;k--)putchar(b[k]); if(nl)putchar('\n'); } free(b); return 0; }
static int cu_tac(int argc,char** argv){ FILE* f=argc>1?openin(argv[1]):stdin; if(!f)return 1; lines_t L={0}; readall(f,&L,0); for(size_t j=L.n;j>0;j--)puts(L.v[j-1]); return 0; }
static int cu_nl(int argc,char** argv){ FILE* f=argc>1?openin(argv[argc-1]):stdin; if(!f)return 1; char* b=0; size_t c=0; long n; long k=0; while((n=getline(&b,&c,f))>0){ if(b[n-1]=='\n')b[--n]=0; if(n)printf("%6ld\t%s\n",++k,b); else printf("       \n"); } free(b); return 0; }
static int cu_fold(int argc,char** argv){ int w=80,i=1; for(;i<argc&&argv[i][0]=='-';i++){ if(!strcmp(argv[i],"-w")&&i+1<argc)w=atoi(argv[++i]); else if(argv[i][1]=='w')w=atoi(argv[i]+2); else w=atoi(argv[i]+1); }
    FILE* f=i<argc?openin(argv[i]):stdin; if(!f)return 1; int ch, col=0; while((ch=getc(f))!=EOF){ if(ch=='\n'){ putchar(ch); col=0; continue; } if(col>=w){ putchar('\n'); col=0; } putchar(ch); col++; } return 0; }
static int cu_paste(int argc,char** argv){ char d='\t'; int i=1, ser=0; for(;i<argc&&argv[i][0]=='-'&&argv[i][1];i++){ if(argv[i][1]=='d')d=argv[i][2]?argv[i][2]:argv[++i][0]; else if(argv[i][1]=='s')ser=1; }
    int nf=argc-i; if(nf<=0)return 0; lines_t* L=calloc((size_t)nf,sizeof(lines_t)); size_t mx=0;
    for(int k=0;k<nf;k++){ FILE* f=openin(argv[i+k]); if(f){ readall(f,&L[k],0); if(f!=stdin)fclose(f); } if(L[k].n>mx)mx=L[k].n; }
    if(ser){ for(int k=0;k<nf;k++){ for(size_t j=0;j<L[k].n;j++){ if(j)putchar(d); fputs(L[k].v[j],stdout); } putchar('\n'); } return 0; }
    for(size_t j=0;j<mx;j++){ for(int k=0;k<nf;k++){ if(k)putchar(d); if(j<L[k].n)fputs(L[k].v[j],stdout); } putchar('\n'); } return 0; }
static int cu_comm(int argc,char** argv){ int s1=0,s2=0,s3=0,i=1; for(;i<argc&&argv[i][0]=='-'&&argv[i][1];i++) for(int k=1;argv[i][k];k++){ if(argv[i][k]=='1')s1=1; if(argv[i][k]=='2')s2=1; if(argv[i][k]=='3')s3=1; }
    if(argc-i<2){ fprintf(stderr,"comm: missing operand\n"); return 1; } FILE* a=openin(argv[i]); FILE* b=openin(argv[i+1]); if(!a||!b)return 1; lines_t A={0},B={0}; readall(a,&A,0); readall(b,&B,0);
    size_t x=0,y=0; while(x<A.n||y<B.n){ int r=x>=A.n?1:y>=B.n?-1:strcmp(A.v[x],B.v[y]);
        if(r<0){ if(!s1)puts(A.v[x]); x++; } else if(r>0){ if(!s2)printf("%s%s\n",s1?"":"\t",B.v[y]); y++; } else { if(!s3)printf("%s%s%s\n",s1?"":"\t",s2?"":"\t",A.v[x]); x++; y++; } }
    return 0; }
static int cu_cmp(int argc,char** argv){ if(argc<3){ fprintf(stderr,"cmp: missing operand\n"); return 2; } FILE* a=openin(argv[1]); FILE* b=openin(argv[2]); if(!a||!b)return 2; long pos=1, line=1; int x,y;
    for(;;){ x=getc(a); y=getc(b); if(x==EOF||y==EOF)break; if(x!=y){ printf("%s %s differ: char %ld, line %ld\n",argv[1],argv[2],pos,line); return 1; } if(x=='\n')line++; pos++; }
    if(x!=y){ fprintf(stderr,"cmp: EOF on %s after byte %ld, line %ld\n",x==EOF?argv[1]:argv[2],pos-1,x==EOF?line:line); return 1; } return 0; }
static int slurp(const char* p,unsigned char** out,long* n){ FILE* f=openin(p); if(!f)return -1; long cap=4096,len=0; unsigned char* b=malloc((size_t)cap); int c; while((c=getc(f))!=EOF){ if(len==cap){ cap*=2; b=realloc(b,(size_t)cap); } b[len++]=(unsigned char)c; } if(f!=stdin)fclose(f); *out=b; *n=len; return 0; }
static int cu_hash(int argc,char** argv,int md){ int i=1; if(i==argc){ argv[argc]="-"; argc++; }
    for(;i<argc;i++){ unsigned char* d; long n; if(slurp(argv[i],&d,&n))continue; char hx[65]; if(md)md5_hex(d,(int)n,hx); else sha256_hex(d,(int)n,hx); printf("%s  %s\n",hx,argv[i]); free(d); } return cu_rc?1:0; }
static int cu_base64(int argc,char** argv){ int dec=0,i=1; for(;i<argc&&argv[i][0]=='-'&&argv[i][1];i++) if(argv[i][1]=='d')dec=1; unsigned char* d; long n; if(slurp(i<argc?argv[i]:"-",&d,&n))return 1;
    if(dec){ d=realloc(d,(size_t)n+1); d[n]=0; char* o=malloc((size_t)n+4); long m=base64_decode((char*)d,o); fwrite(o,1,(size_t)(m>0?m:0),stdout); return 0; }
    char* o=malloc((size_t)(n*4/3+8)); size_t m=base64_encode(d,(size_t)n,o); for(size_t k=0;k<m;k+=76){ fwrite(o+k,1,m-k<76?m-k:76,stdout); putchar('\n'); } return 0; }
static int fnm(const char* p,const char* s){ for(;*p;p++,s++){ if(*p=='*'){ while(p[1]=='*')p++; if(!p[1])return 1; for(;*s;s++) if(fnm(p+1,s))return 1; return fnm(p+1,s); } if(!*s)return 0; if(*p=='?')continue; if(*p=='['){ int neg=p[1]=='!'||p[1]=='^', ok=0; const char* q=p+1+neg; for(;*q&&*q!=']';q++){ if(q[1]=='-'&&q[2]&&q[2]!=']'){ if(*s>=*q&&*s<=q[2])ok=1; q+=2; } else if(*q==*s)ok=1; } if(ok==neg)return 0; p=q; continue; } if(*p!=*s)return 0; } return !*s; }
static const char* f_name; static int f_type, f_maxd;
static void walk(const char* p,int depth){ struct stat st; if(lstat(p,&st)<0){ cu_err(p,"No such file or directory"); return; }
    int isd=S_ISDIR(st.st_mode); const char* b=strrchr(p,'/'); b=b&&b[1]?b+1:p;
    if((!f_name||fnm(f_name,b))&&(!f_type||(f_type=='d'?isd:f_type=='f'?S_ISREG(st.st_mode):S_ISLNK(st.st_mode))))puts(p);
    if(!isd||(f_maxd>=0&&depth>=f_maxd))return;
    DIR* d=opendir(p); if(!d)return; char** names=0; size_t nn=0; struct dirent* e;
    while((e=readdir(d))){ if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue; names=realloc(names,(nn+1)*sizeof(char*)); names[nn++]=strdup(e->d_name); } closedir(d);
    for(size_t k=0;k<nn;k++){ size_t L=strlen(p)+strlen(names[k])+2; char* c=malloc(L); snprintf(c,L,"%s%s%s",p,p[strlen(p)-1]=='/'?"":"/",names[k]); walk(c,depth+1); free(c); free(names[k]); } free(names); }
static int cu_find(int argc,char** argv){ int i=1; const char* roots[32]; int nr=0; f_name=0; f_type=0; f_maxd=-1;
    for(;i<argc&&argv[i][0]!='-'&&nr<32;i++)roots[nr++]=argv[i];
    for(;i<argc;i++){ if(!strcmp(argv[i],"-name")&&i+1<argc)f_name=argv[++i]; else if(!strcmp(argv[i],"-type")&&i+1<argc)f_type=argv[++i][0]; else if(!strcmp(argv[i],"-maxdepth")&&i+1<argc)f_maxd=atoi(argv[++i]); else { fprintf(stderr,"find: unknown predicate `%s'\n",argv[i]); return 1; } }
    if(!nr)roots[nr++]="."; for(int k=0;k<nr;k++)walk(roots[k],0); return cu_rc?1:0; }
static int cu_yes(int argc,char** argv){ for(;;){ if(argc>1){ for(int i=1;i<argc;i++){ if(i>1)putchar(' '); fputs(argv[i],stdout); } putchar('\n'); } else puts("y"); if(ferror(stdout))return 0; } }
static int cu_printenv(int argc,char** argv){ if(argc>1){ int r=0; for(int i=1;i<argc;i++){ char* v=getenv(argv[i]); if(v)puts(v); else r=1; } return r; } const char* ks[]={"PATH","HOME","USER","SHELL","PWD","TERM",0}; for(int k=0;ks[k];k++){ char* v=getenv(ks[k]); if(v)printf("%s=%s\n",ks[k],v); } return 0; }
int coreutils_main(int argc,char** argv){
    const char* c=argv[0]; const char* b=strrchr(c,'/'); if(b)c=b+1; cu_me=c; cu_rc=0;
    static char* av[256]; int n=argc<250?argc:250; for(int i=0;i<n;i++)av[i]=argv[i]; av[n]=0;
    if(!strcmp(c,"head"))return cu_head(n,av); if(!strcmp(c,"tail"))return cu_tail(n,av); if(!strcmp(c,"wc"))return cu_wc(n,av);
    if(!strcmp(c,"sort"))return cu_sort(n,av); if(!strcmp(c,"grep"))return cu_grep(n,av); if(!strcmp(c,"cut"))return cu_cut(n,av);
    if(!strcmp(c,"tr"))return cu_tr(n,av); if(!strcmp(c,"uniq"))return cu_uniq(n,av); if(!strcmp(c,"seq"))return cu_seq(n,av);
    if(!strcmp(c,"basename"))return cu_basename(n,av); if(!strcmp(c,"dirname"))return cu_dirname(n,av); if(!strcmp(c,"rev"))return cu_rev(n,av);
    if(!strcmp(c,"tac"))return cu_tac(n,av); if(!strcmp(c,"nl"))return cu_nl(n,av); if(!strcmp(c,"fold"))return cu_fold(n,av);
    if(!strcmp(c,"paste"))return cu_paste(n,av); if(!strcmp(c,"comm"))return cu_comm(n,av); if(!strcmp(c,"cmp"))return cu_cmp(n,av);
    if(!strcmp(c,"md5sum"))return cu_hash(n,av,1); if(!strcmp(c,"sha256sum"))return cu_hash(n,av,0); if(!strcmp(c,"base64"))return cu_base64(n,av);
    if(!strcmp(c,"find"))return cu_find(n,av); if(!strcmp(c,"yes"))return cu_yes(n,av); if(!strcmp(c,"printenv")||!strcmp(c,"env"))return cu_printenv(n,av);
    return -1000; }
