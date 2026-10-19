#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/lib/js/js.h"
/* js.h - a small integer JavaScript interpreter for NoovexOS.
   Supports: numbers(int), strings, booleans, null/undefined; var/let/const;
   + - * / % , comparisons, && || !, assignment (= += -= *= /=), ++ --,
   if/else, while, for(;;), blocks, functions (decl/expr/call/return/recursion),
   break/continue; document.write/writeln, console.log, Math.*, parseInt,
   String(), Number(), and string methods (.length/.charAt/.substring/
   .toUpperCase/.toLowerCase/.indexOf). Integers only (no floating point).
   Entry: js_run(src, len, out_cb). out_cb receives document.write output. */
#define JS_MAXTOK   40000
#define JS_ARENA    393216
#define JS_MAXG     512
#define JS_MAXL     512
#define JS_MAXDEPTH 96
#define JS_STEPMAX  8000000

enum { JV_UNDEF, JV_NULL, JV_NUM, JV_BOOL, JV_STR, JV_FUNC, JV_ARR, JV_OBJ };
typedef struct { unsigned char t; int num; const char* s; int slen; int ftok; } JVal;
/* arrays and objects (fixed-capacity, per-run pools) */
#define JS_MAXARR 160
#define JS_ARRCAP 256
#define JS_MAXOBJ 80
#define JS_OBJCAP 28
static struct { JVal it[JS_ARRCAP]; int len; } js_arr[JS_MAXARR]; static int js_narr=0;
static struct { char k[JS_OBJCAP][24]; JVal v[JS_OBJCAP]; int n; } js_obj[JS_MAXOBJ]; static int js_nobj=0;

enum { JT_EOF, JT_NUM, JT_STR, JT_ID, JT_P };

enum { P_EQ=256,P_NE,P_LE,P_GE,P_AND,P_OR,P_INC,P_DEC,P_PE,P_ME,P_STE,P_SLE,P_SEQ,P_SNE };

static JTok js_T[JS_MAXTOK]; static int js_nt;
static char js_ar[JS_ARENA]; static int js_arn;
static int js_tp;                     /* current token index */
static int js_sig; static JVal js_ret;/* 1=return 2=break 3=continue */
static long js_steps;
static void (*js_out)(const char*,int);

typedef struct { char name[40]; JVal val; } JVar;
static JVar js_g[JS_MAXG]; static int js_gn;
static JVar js_l[JS_MAXL]; static int js_ln;
static int js_lbase[JS_MAXDEPTH]; static int js_ld;
int js_err;

static int js_slen(const char*s){int n=0;while(s[n])n++;return n;}
static int js_ideq(const JTok*t,const char*w){ if(t->t!=JT_ID)return 0; int i=0; for(;w[i];i++){ if(i>=t->slen||t->s[i]!=w[i])return 0; } return t->slen==i; }
static int js_isidst(char c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||c=='$'; }
static int js_isidp(char c){ return js_isidst(c)||(c>='0'&&c<='9'); }
static char* js_alloc(int n){ if(js_arn+n>JS_ARENA){ js_err=1; return js_ar; } char* p=js_ar+js_arn; js_arn+=n; return p; }
static const char* js_intern(const char*s,int n){ char* p=js_alloc(n+1); for(int i=0;i<n;i++)p[i]=s[i]; p[n]=0; return p; }

/* ---- tokenizer ---- */
static int js_tokenize(const char* src,int n){
    int i=0; js_nt=0;
    while(i<n && js_nt<JS_MAXTOK-1){
        char c=src[i];
        if(c==' '||c=='\t'||c=='\n'||c=='\r'){i++;continue;}
        if(c=='/'&&i+1<n&&src[i+1]=='/'){ while(i<n&&src[i]!='\n')i++; continue; }
        if(c=='/'&&i+1<n&&src[i+1]=='*'){ i+=2; while(i+1<n&&!(src[i]=='*'&&src[i+1]=='/'))i++; i+=2; continue; }
        if(c>='0'&&c<='9'){ int v=0; if(c=='0'&&i+1<n&&(src[i+1]=='x'||src[i+1]=='X')){ i+=2; while(i<n){ char h=src[i]; int d; if(h>='0'&&h<='9')d=h-'0'; else if(h>='a'&&h<='f')d=h-'a'+10; else if(h>='A'&&h<='F')d=h-'A'+10; else break; v=v*16+d; i++; } }
            else { while(i<n&&src[i]>='0'&&src[i]<='9'){v=v*10+(src[i]-'0');i++;} if(i<n&&src[i]=='.'){ i++; while(i<n&&src[i]>='0'&&src[i]<='9')i++; } }
            js_T[js_nt].t=JT_NUM; js_T[js_nt].num=v; js_nt++; continue; }
        if(c=='"'||c=='\''||c=='`'){ char q=c; i++; char tmp[1024]; int k=0;
            while(i<n&&src[i]!=q&&k<1023){ char d=src[i];
                if(d=='\\'&&i+1<n){ i++; char e=src[i]; if(e=='n')d='\n'; else if(e=='t')d='\t'; else if(e=='r')d='\r'; else if(e=='\\')d='\\'; else if(e=='0')d=0; else d=e; }
                tmp[k++]=d; i++; }
            if(i<n)i++; const char* sp=js_intern(tmp,k); js_T[js_nt].t=JT_STR; js_T[js_nt].s=sp; js_T[js_nt].slen=k; js_nt++; continue; }
        if(js_isidst(c)){ int s0=i; while(i<n&&js_isidp(src[i]))i++; js_T[js_nt].t=JT_ID; js_T[js_nt].s=src+s0; js_T[js_nt].slen=i-s0; js_nt++; continue; }
        /* punctuation: 3,2,1 char */
        int p=0,adv=1; char c1=(i+1<n)?src[i+1]:0; char c2=(i+2<n)?src[i+2]:0;
        if(c=='='&&c1=='='&&c2=='='){p=P_SEQ;adv=3;} else if(c=='!'&&c1=='='&&c2=='='){p=P_SNE;adv=3;}
        else if(c=='='&&c1=='='){p=P_EQ;adv=2;} else if(c=='!'&&c1=='='){p=P_NE;adv=2;}
        else if(c=='<'&&c1=='='){p=P_LE;adv=2;} else if(c=='>'&&c1=='='){p=P_GE;adv=2;}
        else if(c=='&'&&c1=='&'){p=P_AND;adv=2;} else if(c=='|'&&c1=='|'){p=P_OR;adv=2;}
        else if(c=='+'&&c1=='+'){p=P_INC;adv=2;} else if(c=='-'&&c1=='-'){p=P_DEC;adv=2;}
        else if(c=='+'&&c1=='='){p=P_PE;adv=2;} else if(c=='-'&&c1=='='){p=P_ME;adv=2;}
        else if(c=='*'&&c1=='='){p=P_STE;adv=2;} else if(c=='/'&&c1=='='){p=P_SLE;adv=2;}
        else { p=(unsigned char)c; adv=1; }
        js_T[js_nt].t=JT_P; js_T[js_nt].p=p; js_nt++; i+=adv;
    }
    js_T[js_nt].t=JT_EOF; js_T[js_nt].p=0; return js_nt;
}

/* ---- value helpers ---- */
static JVal jv_undef(void){ JVal v; v.t=JV_UNDEF; v.num=0; v.s=0; v.slen=0; v.ftok=0; return v; }
static JVal jv_num(int n){ JVal v=jv_undef(); v.t=JV_NUM; v.num=n; return v; }
static JVal jv_bool(int b){ JVal v=jv_undef(); v.t=JV_BOOL; v.num=b?1:0; return v; }
static JVal jv_str(const char* s,int n){ JVal v=jv_undef(); v.t=JV_STR; v.s=s; v.slen=n; return v; }
static JVal jv_arr_new(void){ int i=(js_narr<JS_MAXARR)?js_narr++:0; js_arr[i].len=0; JVal v=jv_undef(); v.t=JV_ARR; v.num=i; return v; }
static JVal jv_obj_new(void){ int i=(js_nobj<JS_MAXOBJ)?js_nobj++:0; js_obj[i].n=0; JVal v=jv_undef(); v.t=JV_OBJ; v.num=i; return v; }
static int js_streq2(JVal s,const char* c){ int i=0; for(;c[i];i++){ if(i>=s.slen||s.s[i]!=c[i])return 0; } return s.slen==i; }
static JVal obj_get(int oi,const char* key,int kl){ if(oi<0||oi>=JS_MAXOBJ)return jv_undef(); for(int i=0;i<js_obj[oi].n;i++){ int m=1; for(int q=0;q<kl;q++)if(js_obj[oi].k[i][q]!=key[q]){m=0;break;} if(m&&js_obj[oi].k[i][kl]==0)return js_obj[oi].v[i]; } return jv_undef(); }
static void obj_set(int oi,const char* key,int kl,JVal val){ if(oi<0||oi>=JS_MAXOBJ)return; if(kl>23)kl=23; for(int i=0;i<js_obj[oi].n;i++){ int m=1; for(int q=0;q<kl;q++)if(js_obj[oi].k[i][q]!=key[q]){m=0;break;} if(m&&js_obj[oi].k[i][kl]==0){ js_obj[oi].v[i]=val; return; } } if(js_obj[oi].n<JS_OBJCAP){ int q=js_obj[oi].n; for(int i=0;i<kl;i++)js_obj[oi].k[q][i]=key[i]; js_obj[oi].k[q][kl]=0; js_obj[oi].v[q]=val; js_obj[oi].n++; } }
static int jv_tonum(JVal v){ if(v.t==JV_NUM||v.t==JV_BOOL)return v.num; if(v.t==JV_STR){ int i=0,sgn=1,val=0,any=0; while(i<v.slen&&(v.s[i]==' '))i++; if(i<v.slen&&(v.s[i]=='-')){sgn=-1;i++;} else if(i<v.slen&&v.s[i]=='+')i++; while(i<v.slen&&v.s[i]>='0'&&v.s[i]<='9'){val=val*10+(v.s[i]-'0');i++;any=1;} return any?sgn*val:0; } return 0; }
static int jv_tobool(JVal v){ if(v.t==JV_NUM)return v.num!=0; if(v.t==JV_BOOL)return v.num; if(v.t==JV_STR)return v.slen>0; if(v.t==JV_UNDEF||v.t==JV_NULL)return 0; return 1; }
static int js_itoa(int n,char*b){ int k=0; if(n<0){ b[k++]='-'; unsigned u=(unsigned)(-(long)n); char t[16]; int tn=0; if(u==0)t[tn++]='0'; while(u){t[tn++]='0'+u%10;u/=10;} while(tn)b[k++]=t[--tn]; } else { char t[16]; int tn=0; if(n==0)t[tn++]='0'; while(n){t[tn++]='0'+n%10;n/=10;} while(tn)b[k++]=t[--tn]; } b[k]=0; return k; }
static const char* jv_tostr(JVal v,int* outlen){ char b[24];
    if(v.t==JV_STR){ *outlen=v.slen; return v.s; }
    if(v.t==JV_NUM){ int k=js_itoa(v.num,b); const char* s=js_intern(b,k); *outlen=k; return s; }
    if(v.t==JV_BOOL){ const char* s=v.num?"true":"false"; int k=js_slen(s); const char* r=js_intern(s,k); *outlen=k; return r; }
    if(v.t==JV_NULL){ *outlen=4; return js_intern("null",4); }
    if(v.t==JV_FUNC){ *outlen=8; return js_intern("function",8); }
    if(v.t==JV_ARR){ int ai=v.num; char* o=js_alloc(512); int on=0; for(int i=0;i<js_arr[ai].len;i++){ if(i&&on<511)o[on++]=','; int L; const char* es=jv_tostr(js_arr[ai].it[i],&L); for(int q=0;q<L&&on<511;q++)o[on++]=es[q]; } o[on]=0; *outlen=on; return o; }
    if(v.t==JV_OBJ){ const char* s="[object Object]"; int k=js_slen(s); *outlen=k; return js_intern(s,k); }
    *outlen=9; return js_intern("undefined",9);
}
static int jv_streq(JVal a,JVal b){ if(a.slen!=b.slen)return 0; for(int i=0;i<a.slen;i++)if(a.s[i]!=b.s[i])return 0; return 1; }

/* ---- scopes ---- */
static JVar* js_find(const char* name,int nl){
    if(js_ld>0){ for(int i=js_ln-1;i>=js_lbase[js_ld-1];i--){ int j=0; for(;j<nl&&js_l[i].name[j];j++)if(js_l[i].name[j]!=name[j])break; if(j==nl&&js_l[i].name[j]==0)return &js_l[i]; } }
    for(int i=js_gn-1;i>=0;i--){ int j=0; for(;j<nl&&js_g[i].name[j];j++)if(js_g[i].name[j]!=name[j])break; if(j==nl&&js_g[i].name[j]==0)return &js_g[i]; }
    return 0;
}
static void js_declare(const char* name,int nl,JVal val){ JVar* a; if(js_ld>0){ if(js_ln>=JS_MAXL){js_err=1;return;} a=&js_l[js_ln++]; } else { if(js_gn>=JS_MAXG){js_err=1;return;} a=&js_g[js_gn++]; } int k=nl; if(k>39)k=39; for(int i=0;i<k;i++)a->name[i]=name[i]; a->name[k]=0; a->val=val; }
static void js_assign(const char* name,int nl,JVal val){ JVar* v=js_find(name,nl); if(v)v->val=val; else { if(js_gn>=JS_MAXG){js_err=1;return;} JVar* a=&js_g[js_gn++]; int k=nl; if(k>39)k=39; for(int i=0;i<k;i++)a->name[i]=name[i]; a->name[k]=0; a->val=val; } }

/* ---- forward decls ---- */
static JVal ev_assign(void);
static void exec_stmt(void);
static void skip_block(void);   /* skip a { } or single stmt from current tp */
static void exec_stmt_forinit(void);
static void skip_to_rparen(void);
static int js_ideqs(const char* a,int al,const char* b);

JTok* cur(void){ return &js_T[js_tp]; }
static int at_p(int code){ return js_T[js_tp].t==JT_P && js_T[js_tp].p==code; }
static int eat_p(int code){ if(at_p(code)){ js_tp++; return 1; } return 0; }
static int js_isctype(JTok* t){ return t->t==JT_ID && (js_ideq(t,"int")||js_ideq(t,"char")||js_ideq(t,"long")||js_ideq(t,"short")||js_ideq(t,"float")||js_ideq(t,"double")||js_ideq(t,"void")||js_ideq(t,"unsigned")||js_ideq(t,"signed")||js_ideq(t,"bool")); }

/* parse call args into array; assumes cur is '(' */
static int parse_args(JVal* args,int maxa){ int n=0; eat_p('('); if(!at_p(')')){ do{ JVal v=ev_assign(); if(n<maxa)args[n]=v; n++; }while(eat_p(',')); } eat_p(')'); return n; }

/* call a user function value */
static JVal call_func(JVal fn,JVal* args,int na){
    if(fn.t!=JV_FUNC){ return jv_undef(); }
    int savetp=js_tp; js_tp=fn.ftok;              /* at '(' of params */
    if(js_ld>=JS_MAXDEPTH){ js_err=1; js_tp=savetp; return jv_undef(); }
    js_lbase[js_ld++]=js_ln;
    eat_p('(');
    int pi=0;
    if(!at_p(')')){ do{ while(js_isctype(cur()))js_tp++; if(cur()->t==JT_ID){ JVal av=(pi<na)?args[pi]:jv_undef(); js_declare(cur()->s,cur()->slen,av); js_tp++; pi++; } else js_tp++; }while(eat_p(',')); }
    eat_p(')');
    /* body block */
    int sig0=js_sig; js_sig=0; js_ret=jv_undef();
    exec_stmt();                                   /* the { } body */
    JVal r=(js_sig==1)?js_ret:jv_undef();
    js_sig=sig0;
    js_ln=js_lbase[--js_ld];
    js_tp=savetp;
    return r;
}

/* built-in: base object name.method(args) or global func(args). Returns 1 if handled. */
static int builtin_call(const char* obj,int objl,const char* meth,int methl,JVal* args,int na,JVal* out){
    /* helper macros */
    #define OBJ(x) (objl==(int)(sizeof(x)-1) && js_ideqs(obj,objl,x))
    (void)obj;(void)objl;
    /* document.write / writeln */
    if(objl==8 && obj[0]=='d'&&obj[1]=='o'&&obj[2]=='c'&&obj[3]=='u'){
        int wr=(methl==5&&meth[0]=='w')||(methl==7&&meth[0]=='w'); if(wr){ for(int i=0;i<na;i++){ int L; const char* s=jv_tostr(args[i],&L); if(js_out)js_out(s,L); } if(methl==7&&js_out)js_out("\n",1); *out=jv_undef(); return 1; } }
    if(objl==7 && obj[0]=='c'&&obj[1]=='o'&&obj[2]=='n'){ *out=jv_undef(); return 1; } /* console.* -> ignore */
    if(objl==4 && obj[0]=='M'&&obj[1]=='a'&&obj[2]=='t'&&obj[3]=='h'){
        int a0=na>0?jv_tonum(args[0]):0, a1=na>1?jv_tonum(args[1]):0;
        if(methl==5&&meth[0]=='f'){ *out=jv_num(a0); return 1; }              /* floor (ints) */
        if(methl==4&&meth[0]=='c'){ *out=jv_num(a0); return 1; }              /* ceil */
        if(methl==5&&meth[0]=='r'&&meth[1]=='o'){ *out=jv_num(a0); return 1; }/* round */
        if(methl==3&&meth[0]=='a'){ *out=jv_num(a0<0?-a0:a0); return 1; }     /* abs */
        if(methl==3&&meth[0]=='m'&&meth[1]=='a'){ *out=jv_num(a0>a1?a0:a1); return 1; } /* max */
        if(methl==3&&meth[0]=='m'&&meth[1]=='i'){ *out=jv_num(a0<a1?a0:a1); return 1; } /* min */
        if(methl==4&&meth[0]=='s'&&meth[1]=='q'){ int r=0; while((r+1)*(r+1)<=a0)r++; *out=jv_num(r); return 1; } /* sqrt */
        if(methl==3&&meth[0]=='p'){ int r=1; if(a1<0)r=0; else for(int k=0;k<a1&&k<64;k++)r*=a0; *out=jv_num(r); return 1; } /* pow */
        if(methl==4&&meth[0]=='s'&&meth[1]=='i'){ *out=jv_num(a0>0?1:a0<0?-1:0); return 1; } /* sign */
        if(methl==5&&meth[0]=='t'){ *out=jv_num(a0); return 1; } /* trunc */
        if(methl==5&&meth[0]=='h'){ int v=a0*a0+a1*a1, r=0; while((r+1)*(r+1)<=v)r++; *out=jv_num(r); return 1; } /* hypot */
        if(methl==6&&meth[0]=='r'){ static unsigned rs=2463534242u; rs^=rs<<13; rs^=rs>>17; rs^=rs<<5; *out=jv_num((int)(rs%1000)); return 1; } /* random -> 0..999 (int) */
        *out=jv_num(a0); return 1;
    }
    *out=jv_undef(); return 0;
    #undef OBJ
}
static int js_ideqs(const char* a,int al,const char* b){ int i=0; for(;b[i];i++){ if(i>=al||a[i]!=b[i])return 0; } return al==i; }

/* string method: strval.method(args) */
static JVal string_method(JVal s,const char* meth,int methl,JVal* args,int na){
    if(methl==6&&meth[0]=='c'&&meth[1]=='h'){ int i=na>0?jv_tonum(args[0]):0; if(i>=0&&i<s.slen)return jv_str(js_intern(s.s+i,1),1); return jv_str(js_intern("",0),0); } /* charAt */
    if((methl==11&&meth[0]=='t'&&meth[3]=='p')){ char* b=js_alloc(s.slen+1); for(int i=0;i<s.slen;i++){ char c=s.s[i]; if(c>='a'&&c<='z')c-=32; b[i]=c; } b[s.slen]=0; return jv_str(b,s.slen); } /* toUpperCase */
    if((methl==11&&meth[0]=='t'&&meth[3]=='o')){ char* b=js_alloc(s.slen+1); for(int i=0;i<s.slen;i++){ char c=s.s[i]; if(c>='A'&&c<='Z')c+=32; b[i]=c; } b[s.slen]=0; return jv_str(b,s.slen); } /* toLowerCase */
    if(methl==9&&meth[0]=='s'&&meth[1]=='u'){ int a=na>0?jv_tonum(args[0]):0, e=na>1?jv_tonum(args[1]):s.slen; if(a<0)a=0; if(e>s.slen)e=s.slen; if(e<a)e=a; return jv_str(js_intern(s.s+a,e-a),e-a); } /* substring */
    if(methl==7&&meth[0]=='i'){ if(na>0&&args[0].t==JV_STR){ JVal q=args[0]; for(int i=0;i+q.slen<=s.slen;i++){ int m=1; for(int j=0;j<q.slen;j++)if(s.s[i+j]!=q.s[j]){m=0;break;} if(m)return jv_num(i); } } return jv_num(-1); } /* indexOf */
    #define M(x) js_ideqs(meth,methl,x)
    JVal q=(na>0&&args[0].t==JV_STR)?args[0]:jv_str("",0); int a0=na>0?jv_tonum(args[0]):0;
    if(M("split")){ JVal ar=jv_arr_new(); int ai=ar.num;
        if(na==0){ js_arr[ai].it[0]=s; js_arr[ai].len=1; return ar; }
        if(q.slen==0){ for(int i=0;i<s.slen&&js_arr[ai].len<JS_ARRCAP;i++)js_arr[ai].it[js_arr[ai].len++]=jv_str(js_intern(s.s+i,1),1); return ar; }
        int st=0; for(int i=0;i+q.slen<=s.slen;){ int m=1; for(int j=0;j<q.slen;j++)if(s.s[i+j]!=q.s[j]){m=0;break;}
            if(m){ if(js_arr[ai].len<JS_ARRCAP)js_arr[ai].it[js_arr[ai].len++]=jv_str(js_intern(s.s+st,i-st),i-st); i+=q.slen; st=i; } else i++; }
        if(js_arr[ai].len<JS_ARRCAP)js_arr[ai].it[js_arr[ai].len++]=jv_str(js_intern(s.s+st,s.slen-st),s.slen-st); return ar; }
    if(M("replace")||M("replaceAll")){ int all=methl==10; int rl; const char* r=na>1?jv_tostr(args[1],&rl):""; if(na<2)rl=0;
        int cap=s.slen+1; if(rl>q.slen&&q.slen>0)cap+=(s.slen/q.slen+1)*(rl-q.slen); char* b=js_alloc(cap+1); int bn=0, done=0;
        for(int i=0;i<s.slen;){ int m=q.slen>0&&!done&&i+q.slen<=s.slen; for(int j=0;m&&j<q.slen;j++)if(s.s[i+j]!=q.s[j])m=0;
            if(m){ for(int j=0;j<rl&&bn<cap;j++)b[bn++]=r[j]; i+=q.slen; if(!all)done=1; } else { if(bn<cap)b[bn++]=s.s[i]; i++; } }
        b[bn]=0; return jv_str(b,bn); }
    if(M("trim")||M("trimStart")||M("trimEnd")){ int a=0,e=s.slen; if(methl!=7)while(a<e&&(s.s[a]==' '||s.s[a]=='\t'||s.s[a]=='\n'||s.s[a]=='\r'))a++; if(methl!=9)while(e>a&&(s.s[e-1]==' '||s.s[e-1]=='\t'||s.s[e-1]=='\n'||s.s[e-1]=='\r'))e--; return jv_str(js_intern(s.s+a,e-a),e-a); }
    if(M("startsWith")){ if(q.slen>s.slen)return jv_bool(0); for(int j=0;j<q.slen;j++)if(s.s[j]!=q.s[j])return jv_bool(0); return jv_bool(1); }
    if(M("endsWith")){ if(q.slen>s.slen)return jv_bool(0); int o=s.slen-q.slen; for(int j=0;j<q.slen;j++)if(s.s[o+j]!=q.s[j])return jv_bool(0); return jv_bool(1); }
    if(M("includes")||M("lastIndexOf")){ int r=-1; for(int i=0;i+q.slen<=s.slen;i++){ int m=1; for(int j=0;j<q.slen;j++)if(s.s[i+j]!=q.s[j]){m=0;break;} if(m){ r=i; if(methl==8)break; } } return methl==8?jv_bool(r>=0):jv_num(r); }
    if(M("repeat")){ if(a0<0||a0*s.slen>4096)a0=0; char* b=js_alloc(a0*s.slen+1); int bn=0; for(int k=0;k<a0;k++)for(int j=0;j<s.slen;j++)b[bn++]=s.s[j]; b[bn]=0; return jv_str(b,bn); }
    if(M("slice")||M("substr")){ int a=a0, e=na>1?jv_tonum(args[1]):s.slen; if(a<0)a+=s.slen; if(a<0)a=0; if(a>s.slen)a=s.slen;
        if(methl==6){ if(na>1){ if(e<0)e=0; e=a+e; } else e=s.slen; } else if(e<0)e+=s.slen; if(e>s.slen)e=s.slen; if(e<a)e=a; return jv_str(js_intern(s.s+a,e-a),e-a); }
    if(M("padStart")||M("padEnd")){ int L=a0; const char* f=" "; int fl=1; if(na>1&&args[1].t==JV_STR&&args[1].slen>0){ f=args[1].s; fl=args[1].slen; } if(L<=s.slen||L>4096)return s;
        char* b=js_alloc(L+1); int pad=L-s.slen, bn=0; if(methl==8)for(int j=0;j<pad;j++)b[bn++]=f[j%fl]; for(int j=0;j<s.slen;j++)b[bn++]=s.s[j]; if(methl==6)for(int j=0;j<pad;j++)b[bn++]=f[j%fl]; b[bn]=0; return jv_str(b,bn); }
    if(M("charCodeAt")||M("codePointAt")){ return (a0>=0&&a0<s.slen)?jv_num((unsigned char)s.s[a0]):jv_undef(); }
    if(M("at")){ int i=a0<0?a0+s.slen:a0; return (i>=0&&i<s.slen)?jv_str(js_intern(s.s+i,1),1):jv_undef(); }
    if(M("concat")){ int tot=s.slen; int ls[8]; const char* ss[8]; for(int k=0;k<na;k++){ ss[k]=jv_tostr(args[k],&ls[k]); tot+=ls[k]; } char* b=js_alloc(tot+1); int bn=0; for(int j=0;j<s.slen;j++)b[bn++]=s.s[j]; for(int k=0;k<na;k++)for(int j=0;j<ls[k];j++)b[bn++]=ss[k][j]; b[bn]=0; return jv_str(b,bn); }
    if(M("toString")||M("valueOf")||M("normalize"))return s;
    #undef M
    return jv_undef();
}

/* ---- expression evaluator (precedence climbing) ---- */
static JVal js_postfix(JVal v){
    for(;;){
        if(at_p('.')){ js_tp++; if(cur()->t!=JT_ID)break; const char* m=cur()->s; int ml=cur()->slen; js_tp++;
            if(at_p('(')){ JVal a[8]; int na=parse_args(a,8);
                if(v.t==JV_STR) v=string_method(v,m,ml,a,na);
                else if(v.t==JV_ARR){ int ai=v.num;
                    if(ml==4&&m[0]=='p'&&m[1]=='u'){ if(na>0&&js_arr[ai].len<JS_ARRCAP)js_arr[ai].it[js_arr[ai].len++]=a[0]; v=jv_num(js_arr[ai].len); }
                    else if(ml==3&&m[0]=='p'&&m[1]=='o'){ v=(js_arr[ai].len>0)?js_arr[ai].it[--js_arr[ai].len]:jv_undef(); }
                    else if(js_ideqs(m,ml,"shift")){ if(js_arr[ai].len>0){ v=js_arr[ai].it[0]; for(int i=1;i<js_arr[ai].len;i++)js_arr[ai].it[i-1]=js_arr[ai].it[i]; js_arr[ai].len--; } else v=jv_undef(); }
                    else if(js_ideqs(m,ml,"unshift")){ if(na>0&&js_arr[ai].len<JS_ARRCAP){ for(int i=js_arr[ai].len;i>0;i--)js_arr[ai].it[i]=js_arr[ai].it[i-1]; js_arr[ai].it[0]=a[0]; js_arr[ai].len++; } v=jv_num(js_arr[ai].len); }
                    else if(js_ideqs(m,ml,"reverse")){ int L=js_arr[ai].len; for(int i=0;i<L/2;i++){ JVal t=js_arr[ai].it[i]; js_arr[ai].it[i]=js_arr[ai].it[L-1-i]; js_arr[ai].it[L-1-i]=t; } }
                    else if(js_ideqs(m,ml,"slice")){ int L=js_arr[ai].len, b0=na>0?jv_tonum(a[0]):0, e0=na>1?jv_tonum(a[1]):L; if(b0<0)b0+=L; if(e0<0)e0+=L; if(b0<0)b0=0; if(e0>L)e0=L; JVal nv=jv_arr_new(); for(int i=b0;i<e0&&js_arr[nv.num].len<JS_ARRCAP;i++)js_arr[nv.num].it[js_arr[nv.num].len++]=js_arr[ai].it[i]; v=nv; }
                    else if(js_ideqs(m,ml,"includes")){ int r=0; for(int i=0;i<js_arr[ai].len&&na>0;i++){ JVal e=js_arr[ai].it[i]; if((e.t==JV_NUM&&a[0].t==JV_NUM&&e.num==a[0].num)||(e.t==JV_STR&&a[0].t==JV_STR&&jv_streq(e,a[0]))){r=1;break;} } v=jv_bool(r); }
                    else if(js_ideqs(m,ml,"concat")){ JVal nv=jv_arr_new(); for(int i=0;i<js_arr[ai].len&&js_arr[nv.num].len<JS_ARRCAP;i++)js_arr[nv.num].it[js_arr[nv.num].len++]=js_arr[ai].it[i];
                        for(int k=0;k<na;k++){ if(a[k].t==JV_ARR){ for(int i=0;i<js_arr[a[k].num].len&&js_arr[nv.num].len<JS_ARRCAP;i++)js_arr[nv.num].it[js_arr[nv.num].len++]=js_arr[a[k].num].it[i]; } else if(js_arr[nv.num].len<JS_ARRCAP)js_arr[nv.num].it[js_arr[nv.num].len++]=a[k]; } v=nv; }
                    else if(ml==7&&m[0]=='i'){ int r=-1; for(int i=0;i<js_arr[ai].len&&na>0;i++){ JVal e=js_arr[ai].it[i]; if((e.t==JV_NUM&&a[0].t==JV_NUM&&e.num==a[0].num)||(e.t==JV_STR&&a[0].t==JV_STR&&jv_streq(e,a[0]))){r=i;break;} } v=jv_num(r); }
                    else if(ml==4&&m[0]=='j'){ char* o=js_alloc(1024); int on=0; const char* sep=(na>0&&a[0].t==JV_STR)?a[0].s:","; int sl=(na>0&&a[0].t==JV_STR)?a[0].slen:1; for(int i=0;i<js_arr[ai].len;i++){ if(i)for(int q=0;q<sl&&on<1023;q++)o[on++]=sep[q]; int L; const char* es=jv_tostr(js_arr[ai].it[i],&L); for(int q=0;q<L&&on<1023;q++)o[on++]=es[q]; } o[on]=0; v=jv_str(o,on); }
                    else v=jv_undef();
                } else v=jv_undef();
            } else {
                if(v.t==JV_OBJ) v=obj_get(v.num,m,ml);
                else if(v.t==JV_ARR && ml==6&&m[0]=='l') v=jv_num(js_arr[v.num].len);
                else if(v.t==JV_STR && ml==6&&m[0]=='l') v=jv_num(v.slen);
                else v=jv_undef();
            }
        } else if(at_p('[')){ js_tp++; JVal iv=ev_assign(); eat_p(']');
            if(v.t==JV_ARR){ int i=jv_tonum(iv); v=(i>=0&&i<js_arr[v.num].len)?js_arr[v.num].it[i]:jv_undef(); }
            else if(v.t==JV_OBJ){ int L; const char* k=jv_tostr(iv,&L); v=obj_get(v.num,k,L); }
            else if(v.t==JV_STR){ int i=jv_tonum(iv); v=(i>=0&&i<v.slen)?jv_str(js_intern(v.s+i,1),1):jv_str(js_intern("",0),0); }
            else v=jv_undef();
        } else break;
    }
    return v;
}
static JVal ev_primary(void){
    JTok* t=cur();
    if(at_p('[')){ js_tp++; JVal arr=jv_arr_new(); int ai=arr.num; if(!at_p(']')){ do{ JVal e=ev_assign(); if(js_arr[ai].len<JS_ARRCAP)js_arr[ai].it[js_arr[ai].len++]=e; }while(eat_p(',')); } eat_p(']'); return js_postfix(arr); }
    if(at_p('{')){ js_tp++; JVal ob=jv_obj_new(); int oi=ob.num; if(!at_p('}')){ do{ char key[24]; int kl=0; if(cur()->t==JT_STR){ for(int i=0;i<cur()->slen&&kl<23;i++)key[kl++]=cur()->s[i]; js_tp++; } else if(cur()->t==JT_ID){ for(int i=0;i<cur()->slen&&kl<23;i++)key[kl++]=cur()->s[i]; js_tp++; } else if(cur()->t==JT_NUM){ kl=js_itoa(cur()->num,key); js_tp++; } else break; key[kl]=0; eat_p(':'); JVal val=ev_assign(); obj_set(oi,key,kl,val); }while(eat_p(',')); } eat_p('}'); return js_postfix(ob); }
    if(t->t==JT_NUM){ js_tp++; return jv_num(t->num); }
    if(t->t==JT_STR){ js_tp++; JVal v=jv_str(t->s,t->slen);
        /* string literal method / .length */
        while(at_p('.')){ js_tp++; if(cur()->t!=JT_ID)break; const char* m=cur()->s; int ml=cur()->slen; js_tp++;
            if(ml==6&&m[0]=='l'){ v=jv_num(v.slen); }
            else if(at_p('(')){ JVal a[8]; int na=parse_args(a,8); v=string_method(v,m,ml,a,na); }
        } return v; }
    if(at_p('(')){ js_tp++; JVal v=ev_assign(); eat_p(')'); return v; }
    if(at_p('!')){ js_tp++; JVal v=ev_primary(); return jv_bool(!jv_tobool(v)); }
    if(at_p('-')){ js_tp++; JVal v=ev_primary(); return jv_num(-jv_tonum(v)); }
    if(at_p('+')){ js_tp++; JVal v=ev_primary(); return jv_num(jv_tonum(v)); }
    if(at_p(P_INC)||at_p(P_DEC)){ int inc=at_p(P_INC); js_tp++; if(cur()->t==JT_ID){ const char* nm=cur()->s; int nl=cur()->slen; js_tp++; JVar* vp=js_find(nm,nl); int nv=(vp?jv_tonum(vp->val):0)+(inc?1:-1); JVal r=jv_num(nv); js_assign(nm,nl,r); return r; } return jv_undef(); }
    if(t->t==JT_ID){
        if(js_ideq(t,"true")){ js_tp++; return jv_bool(1); }
        if(js_ideq(t,"false")){ js_tp++; return jv_bool(0); }
        if(js_ideq(t,"null")){ js_tp++; JVal v=jv_undef(); v.t=JV_NULL; return v; }
        if(js_ideq(t,"undefined")){ js_tp++; return jv_undef(); }
        if(js_ideq(t,"NaN")){ js_tp++; return jv_num(0); }
        if(js_ideq(t,"function")){ js_tp++; if(cur()->t==JT_ID)js_tp++; JVal f=jv_undef(); f.t=JV_FUNC; f.ftok=js_tp; skip_block(); /* skip params+body: skip_block handles '(' then '{' */ return f; }
        const char* nm=t->s; int nl=t->slen; int next=js_tp+1;
        /* object.method(...) built-in */
        if(js_T[next].t==JT_P && js_T[next].p=='.'){
            /* only treat known objects as built-in objects */
            int known=(js_ideq(t,"document")||js_ideq(t,"console")||js_ideq(t,"Math")||js_ideq(t,"window")||js_ideq(t,"JSON"));
            if(known){ js_tp+=2; /* past name and '.' */ if(cur()->t==JT_ID){ const char* m=cur()->s; int ml=cur()->slen; js_tp++;
                    if(at_p('(')){ JVal a[8]; int na=parse_args(a,8); JVal out; builtin_call(nm,nl,m,ml,a,na,&out); return out; }
                    return jv_undef(); } return jv_undef(); }
            /* variable member/index access (arrays, objects, strings) */
            js_tp++; JVar* vp=js_find(nm,nl); JVal v=vp?vp->val:jv_undef();
            return js_postfix(v);
        }
        /* function call: name(...) */
        if(js_T[next].t==JT_P && js_T[next].p=='('){
            /* builtin global funcs */
            if(js_ideq(t,"parseInt")||js_ideq(t,"parseFloat")){ js_tp++; JVal a[8]; int na=parse_args(a,8); return jv_num(na>0?jv_tonum(a[0]):0); }
            if(js_ideq(t,"Number")){ js_tp++; JVal a[8]; int na=parse_args(a,8); return jv_num(na>0?jv_tonum(a[0]):0); }
            if(js_ideq(t,"String")){ js_tp++; JVal a[8]; int na=parse_args(a,8); if(na>0){ int L; const char* s=jv_tostr(a[0],&L); return jv_str(s,L);} return jv_str(js_intern("",0),0); }
            if(js_ideq(t,"Boolean")){ js_tp++; JVal a[8]; int na=parse_args(a,8); return jv_bool(na>0&&jv_tobool(a[0])); }
            if(js_ideq(t,"isNaN")){ js_tp++; JVal a[8]; parse_args(a,8); return jv_bool(0); }
            if(js_ideq(t,"printf")){ js_tp++; JVal a[8]; int na=parse_args(a,8);
                if(na>0&&a[0].t==JV_STR){ const char* f=a[0].s; int fl=a[0].slen,ai=1;
                    for(int i=0;i<fl;i++){ if(f[i]=='%'&&i+1<fl){ char sp=f[i+1]; i++;
                            if(sp=='%'){ if(js_out)js_out("%",1); }
                            else if(sp=='d'||sp=='i'||sp=='u'){ char b[16]; int bn=js_itoa(ai<na?jv_tonum(a[ai++]):0,b); if(js_out)js_out(b,bn); }
                            else if(sp=='s'){ int L; const char* z=jv_tostr(ai<na?a[ai++]:jv_undef(),&L); if(js_out)js_out(z,L); }
                            else if(sp=='c'){ char c=(char)(ai<na?jv_tonum(a[ai++]):0); if(js_out)js_out(&c,1); }
                            else if(js_out){ js_out("%",1); js_out(&sp,1); } }
                        else if(js_out)js_out(&f[i],1); } }
                else { for(int k=0;k<na;k++){ int L; const char* z=jv_tostr(a[k],&L); if(js_out)js_out(z,L); } } return jv_num(0); }
            if(js_ideq(t,"print")||js_ideq(t,"puts")){ int pu=js_ideq(t,"puts"); js_tp++; JVal a[8]; int na=parse_args(a,8); for(int k=0;k<na;k++){ int L; const char* z=jv_tostr(a[k],&L); if(js_out)js_out(z,L); } if(pu&&js_out)js_out("\n",1); return jv_num(0); }
            if(js_ideq(t,"alert")||js_ideq(t,"prompt")){ js_tp++; JVal a[8]; parse_args(a,8); return jv_undef(); }
            JVar* vp=js_find(nm,nl); js_tp++; JVal a[8]; int na=parse_args(a,8);
            if(vp&&vp->val.t==JV_FUNC) return call_func(vp->val,a,na);
            return jv_undef();
        }
        /* plain variable, maybe postfix ++/-- or member/index access */
        js_tp++; JVar* vp=js_find(nm,nl); JVal v=vp?vp->val:jv_undef();
        if(at_p(P_INC)||at_p(P_DEC)){ int inc=at_p(P_INC); js_tp++; int old=jv_tonum(v); js_assign(nm,nl,jv_num(old+(inc?1:-1))); return jv_num(old); }
        return js_postfix(v);
    }
    js_tp++; return jv_undef();
}
static JVal ev_mul(void){ JVal l=ev_primary(); while(at_p('*')||at_p('/')||at_p('%')){ int op=cur()->p; js_tp++; JVal r=ev_primary(); int a=jv_tonum(l),b=jv_tonum(r); if(op=='*')l=jv_num(a*b); else if(op=='/')l=jv_num(b?a/b:0); else l=jv_num(b?a%b:0); } return l; }
static JVal ev_add(void){ JVal l=ev_mul(); while(at_p('+')||at_p('-')){ int op=cur()->p; js_tp++; JVal r=ev_mul();
    if(op=='+'&&(l.t==JV_STR||r.t==JV_STR)){ int la,lb; const char* sa=jv_tostr(l,&la); const char* sb=jv_tostr(r,&lb); char* o=js_alloc(la+lb+1); for(int i=0;i<la;i++)o[i]=sa[i]; for(int i=0;i<lb;i++)o[la+i]=sb[i]; o[la+lb]=0; l=jv_str(o,la+lb); }
    else { int a=jv_tonum(l),b=jv_tonum(r); l=jv_num(op=='+'?a+b:a-b); } } return l; }
static JVal ev_rel(void){ JVal l=ev_add(); while(at_p('<')||at_p('>')||at_p(P_LE)||at_p(P_GE)){ int op=cur()->p; js_tp++; JVal r=ev_add(); int a=jv_tonum(l),b=jv_tonum(r); int res=0; if(op=='<')res=a<b; else if(op=='>')res=a>b; else if(op==P_LE)res=a<=b; else res=a>=b; l=jv_bool(res); } return l; }
static JVal ev_eq(void){ JVal l=ev_rel(); while(at_p(P_EQ)||at_p(P_NE)||at_p(P_SEQ)||at_p(P_SNE)){ int op=cur()->p; js_tp++; JVal r=ev_rel(); int eq; if(l.t==JV_STR&&r.t==JV_STR)eq=jv_streq(l,r); else eq=(jv_tonum(l)==jv_tonum(r)); if(op==P_NE||op==P_SNE)eq=!eq; l=jv_bool(eq); } return l; }
static JVal ev_and(void){ JVal l=ev_eq(); while(at_p(P_AND)){ js_tp++; if(!jv_tobool(l)){ /* short-circuit: still parse rhs */ int d=js_tp; JVal r=ev_eq(); (void)r;(void)d; } else l=ev_eq(); } return l; }
static JVal ev_or(void){ JVal l=ev_and(); while(at_p(P_OR)){ js_tp++; if(jv_tobool(l)){ JVal r=ev_and(); (void)r; } else l=ev_and(); } return l; }
static JVal ev_ternary(void){ JVal c=ev_or(); if(at_p('?')){ js_tp++; JVal a=ev_assign(); eat_p(':'); JVal b=ev_assign(); return jv_tobool(c)?a:b; } return c; }
static JVal ev_assign(void){
    /* lookahead: IDENT followed by =,+=,...  or  ident[..]=  or  ident.prop= */
    if(cur()->t==JT_ID){ int nx=js_tp+1; int nt=js_T[nx].t==JT_P?js_T[nx].p:0;
        if(nt=='['){ const char* nm=cur()->s; int nl=cur()->slen; int save=js_tp; js_tp+=2; JVal iv=ev_assign();
            if(at_p(']')){ js_tp++; if(at_p('=')){ js_tp++; JVal rv=ev_assign(); JVar* vp=js_find(nm,nl);
                    if(vp){ if(vp->val.t==JV_ARR){ int ai=vp->val.num,i=jv_tonum(iv); if(i>=0&&i<JS_ARRCAP){ if(i>=js_arr[ai].len)js_arr[ai].len=i+1; js_arr[ai].it[i]=rv; } }
                        else if(vp->val.t==JV_OBJ){ int L; const char* k=jv_tostr(iv,&L); obj_set(vp->val.num,k,L,rv); } } return rv; } }
            js_tp=save; }
        if(nt=='.' && js_T[nx+1].t==JT_ID && js_T[nx+2].t==JT_P && js_T[nx+2].p=='='){ const char* nm=cur()->s; int nl=cur()->slen; const char* pr=js_T[nx+1].s; int pl=js_T[nx+1].slen; js_tp+=4; JVal rv=ev_assign(); JVar* vp=js_find(nm,nl); if(vp&&vp->val.t==JV_OBJ)obj_set(vp->val.num,pr,pl,rv); return rv; }
        int op=nt;
        if(op=='='||op==P_PE||op==P_ME||op==P_STE||op==P_SLE){ const char* nm=cur()->s; int nl=cur()->slen; js_tp+=2; JVal r=ev_assign();
            if(op=='='){ js_assign(nm,nl,r); return r; }
            JVar* vp=js_find(nm,nl); int a=vp?jv_tonum(vp->val):0; int b=jv_tonum(r); int nv; JVal res;
            if(op==P_PE){ if((vp&&vp->val.t==JV_STR)||r.t==JV_STR){ int la,lb; JVal lv=vp?vp->val:jv_str(js_intern("",0),0); const char* sa=jv_tostr(lv,&la); const char* sb=jv_tostr(r,&lb); char* o=js_alloc(la+lb+1); for(int i=0;i<la;i++)o[i]=sa[i]; for(int i=0;i<lb;i++)o[la+i]=sb[i]; o[la+lb]=0; res=jv_str(o,la+lb); js_assign(nm,nl,res); return res; } nv=a+b; }
            else if(op==P_ME)nv=a-b; else if(op==P_STE)nv=a*b; else nv=b?a/b:0;
            res=jv_num(nv); js_assign(nm,nl,res); return res; } }
    return ev_ternary();
}

/* ---- statement skipping (for uncalled function bodies / dead branches) ---- */
static void skip_paren(void){ if(!at_p('('))return; int d=0; do{ if(at_p('('))d++; else if(at_p(')'))d--; js_tp++; }while(d>0 && cur()->t!=JT_EOF); }
static void skip_block(void){
    if(at_p('(')) skip_paren();                 /* function params */
    if(at_p('{')){ int d=0; do{ if(at_p('{'))d++; else if(at_p('}'))d--; js_tp++; }while(d>0 && cur()->t!=JT_EOF); return; }
    /* single statement: skip to ';' or block */
    while(cur()->t!=JT_EOF && !at_p(';') && !at_p('{')){ if(at_p('(')){ skip_paren(); continue; } js_tp++; } if(at_p(';'))js_tp++; else if(at_p('{'))skip_block();
}

/* ---- statements ---- */
static void exec_block(void){ eat_p('{'); while(!at_p('}') && cur()->t!=JT_EOF && !js_sig){ if(js_steps++>JS_STEPMAX||js_err){js_err=1;break;} exec_stmt(); } /* consume rest if signalled */ if(js_sig){ int d=1; js_tp++; while(d>0&&cur()->t!=JT_EOF){ if(at_p('{'))d++; else if(at_p('}'))d--; js_tp++; } } else eat_p('}'); }

static void exec_stmt(void){
    if(js_steps++>JS_STEPMAX||js_err){ js_err=1; return; }
    JTok* t=cur();
    if(at_p(';')){ js_tp++; return; }
    if(at_p('{')){ exec_block(); return; }
    if(t->t==JT_ID){
        if(js_isctype(t)){
            if(js_T[js_tp+1].t==JT_ID && js_T[js_tp+2].t==JT_P && js_T[js_tp+2].p=='('){ js_tp++; if(cur()->t==JT_ID)js_tp++; skip_block(); return; }  /* C function def (hoisted) */
            js_tp++; while(js_isctype(cur()))js_tp++;                        /* skip extra type words */
            do{ if(cur()->t!=JT_ID)break; const char* nm=cur()->s; int nl=cur()->slen; js_tp++; JVal v=jv_undef();
                if(at_p('[')){ js_tp++; int sz=0; if(cur()->t==JT_NUM){sz=cur()->num;js_tp++;} eat_p(']'); v=jv_arr_new();
                    if(v.t==JV_ARR){ int ai=v.num; if(sz>JS_ARRCAP)sz=JS_ARRCAP; if(sz<0)sz=0; js_arr[ai].len=sz; for(int z=0;z<sz;z++)js_arr[ai].it[z]=jv_num(0); }
                    if(eat_p('=')) v=ev_assign(); }
                else if(eat_p('=')) v=ev_assign();
                js_declare(nm,nl,v); }while(eat_p(',')); eat_p(';'); return; }
        if(js_ideq(t,"var")||js_ideq(t,"let")||js_ideq(t,"const")){ js_tp++;
            do{ if(cur()->t!=JT_ID)break; const char* nm=cur()->s; int nl=cur()->slen; js_tp++; JVal v=jv_undef(); if(eat_p('=')) v=ev_assign(); js_declare(nm,nl,v); }while(eat_p(',')); eat_p(';'); return; }
        if(js_ideq(t,"function")){ js_tp++; if(cur()->t==JT_ID)js_tp++; skip_block(); return; } /* declared in pre-pass */
        if(js_ideq(t,"return")){ js_tp++; JVal v=jv_undef(); if(!at_p(';')&&!at_p('}'))v=ev_assign(); eat_p(';'); js_ret=v; js_sig=1; return; }
        if(js_ideq(t,"break")){ js_tp++; eat_p(';'); js_sig=2; return; }
        if(js_ideq(t,"continue")){ js_tp++; eat_p(';'); js_sig=3; return; }
        if(js_ideq(t,"if")){ js_tp++; eat_p('('); JVal c=ev_assign(); eat_p(')'); if(jv_tobool(c)){ exec_stmt(); if(cur()->t==JT_ID&&js_ideq(cur(),"else")){ js_tp++; skip_block(); } } else { skip_block(); if(cur()->t==JT_ID&&js_ideq(cur(),"else")){ js_tp++; exec_stmt(); } } return; }
        if(js_ideq(t,"while")){ js_tp++; int condtp=js_tp; while(1){ if(js_steps++>JS_STEPMAX||js_err){js_err=1;break;} js_tp=condtp; eat_p('('); JVal c=ev_assign(); eat_p(')'); int bodytp=js_tp; if(!jv_tobool(c)){ js_tp=bodytp; skip_block(); break; } exec_stmt(); if(js_sig==2){ js_sig=0; js_tp=bodytp; skip_block(); break; } if(js_sig==3)js_sig=0; if(js_sig==1)break; } return; }
        if(js_ideq(t,"for")){ js_tp++; eat_p('(');
            /* init */ if(!at_p(';')){ exec_stmt_forinit(); } else js_tp++;
            int condtp=js_tp;
            while(1){ if(js_steps++>JS_STEPMAX||js_err){js_err=1;break;}
                js_tp=condtp; JVal c=jv_bool(1); if(!at_p(';'))c=ev_assign(); eat_p(';'); int incrtp=js_tp;
                /* skip increment to find body */ skip_to_rparen(); int bodytp=js_tp;
                if(!jv_tobool(c)){ js_tp=bodytp; skip_block(); break; }
                js_tp=bodytp; exec_stmt();
                if(js_sig==2){ js_sig=0; js_tp=bodytp; skip_block(); break; } if(js_sig==1)break; if(js_sig==3)js_sig=0;
                js_tp=incrtp; if(!at_p(')'))ev_assign(); /* run increment */
            } return; }
    }
    /* expression statement */
    ev_assign(); eat_p(';');
}
/* for-init helper: a var decl or expression, consuming trailing ';' */
static void exec_stmt_forinit(void){ JTok* t=cur(); if(t->t==JT_ID&&(js_ideq(t,"var")||js_ideq(t,"let")||js_ideq(t,"const")||js_isctype(t))){ js_tp++; while(js_isctype(cur()))js_tp++; do{ if(cur()->t!=JT_ID)break; const char* nm=cur()->s; int nl=cur()->slen; js_tp++; JVal v=jv_undef(); if(eat_p('='))v=ev_assign(); js_declare(nm,nl,v); }while(eat_p(',')); } else { ev_assign(); } eat_p(';'); }
/* skip from after cond ';' to the ')' closing the for-header, leaving tp past ')' */
static void skip_to_rparen(void){ int d=1; while(d>0&&cur()->t!=JT_EOF){ if(at_p('('))d++; else if(at_p(')'))d--; if(d==0){ js_tp++; break; } js_tp++; } }

/* pre-pass: hoist function declarations (function name(...){...}) into globals */
static void js_hoist(void){
    for(int i=0;i+2<js_nt;i++){ if(js_T[i].t==JT_ID && (js_ideq(&js_T[i],"function")||js_isctype(&js_T[i])) && js_T[i+1].t==JT_ID && js_T[i+2].t==JT_P && js_T[i+2].p=='('){
        JVal f=jv_undef(); f.t=JV_FUNC; f.ftok=i+2; js_assign(js_T[i+1].s,js_T[i+1].slen,f); } }
}

void js_run(const char* src,int len,void (*out_cb)(const char*,int)){
    js_out=out_cb; js_arn=0; js_gn=0; js_ln=0; js_ld=0; js_sig=0; js_err=0; js_steps=0; js_tp=0; js_ret=jv_undef(); js_narr=0; js_nobj=0;
    js_tokenize(src,len);
    js_hoist();
    js_tp=0;
    while(cur()->t!=JT_EOF && !js_err){ int before=js_tp; if(js_sig){ js_sig=0; } exec_stmt(); if(js_tp==before){ js_tp++; } if(js_steps++>JS_STEPMAX)js_err=1; }
    if(!js_err){ JVar* mf=js_find("main",4); if(mf && mf->val.t==JV_FUNC){ js_sig=0; call_func(mf->val,0,0); } }   /* C entry point */
}
