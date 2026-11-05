static void sh_redraw(void){ if(g_state==ST_LIVE)live_present(); else if(g_state==ST_DESKTOP)compose_present(); }
static int sh_readpw(char* pw,int cap){ int n=0; sh_redraw();
    for(;;){ int c=kpop(); if(c<0){ cpu_idle(); continue; }
        if(c=='\n'){ term_putc('\n'); sh_redraw(); break; }
        if(c==3){ term_puts("^C\n"); sh_redraw(); pw[0]=0; return -1; }
        if(c=='\b'||c==127){ if(n>0){ n--; term_putc('\b'); } }
        else if(c>=32&&c<127&&n<cap-1){ pw[n++]=(char)c; term_putc('*'); }
        sh_redraw(); }
    pw[n]=0; return n; }
void sh_flush(void){ sh_redraw(); }
int sh_yesno(int def){ sh_redraw(); char b[8]; int n=0;
    for(;;){ int c=kpop(); if(c<0){ cpu_idle(); continue; }
        if(c=='\n'){ term_putc('\n'); sh_redraw(); break; }
        if(c==3){ term_puts("^C\n"); sh_redraw(); return 0; }
        if(c=='\b'||c==127){ if(n>0){ n--; term_putc('\b'); } }
        else if(c>=32&&c<127&&n<7){ b[n++]=(char)c; term_putc((char)c); }
        sh_redraw(); }
    if(!n)return def; return b[0]=='y'||b[0]=='Y'; }
extern void apt_cmd(const char* arg);
extern void dpkg_cmd(const char* arg);
static int sh_words(const char* a,char w[][72],int max){
    int n=0; while(*a&&n<max){ while(*a==' ')a++; if(!*a)break; int o=0;
        if(*a=='"'){ a++; while(*a&&*a!='"'){ if(o<71)w[n][o++]=*a; a++; } if(*a=='"')a++; }
        else while(*a&&*a!=' '){ if(o<71)w[n][o++]=*a; a++; }
        w[n][o]=0; n++; }
    return n;
}
static void sh_err(const char* a,const char* b){ tcol(rgb(240,120,120)); term_puts(a); if(b)term_puts(b); term_putc('\n'); tcol(WHT); }
static void sh_wipe(char* p,int n){ for(int i=0;i<n;i++)p[i]=0; }
static int sh_ask_newpw(const char* who,char* out,int cap){
    char p2[72];
    for(int t=0;t<3;t++){
        term_puts("New password"); if(who){ term_puts(" for "); term_puts(who); } term_puts(": "); if(sh_readpw(out,cap)<0)return -1;
        if((int)slen(out)<4){ sh_err("passwd: the password needs at least 4 characters",0); continue; }
        term_puts("Retype new password: "); if(sh_readpw(p2,72)<0){ sh_wipe(out,cap); return -1; }
        int same=1; for(int i=0;;i++){ if(out[i]!=p2[i]){ same=0; break; } if(!out[i])break; }
        sh_wipe(p2,72); if(same)return 0;
        sh_err("passwd: the passwords do not match",0); }
    sh_wipe(out,cap); return -1;
}
static int sudo_auth(void){
    if(g_uid==0)return 1;
    const char* u=cur_user(); char who[32]; int i=0; for(;u[i]&&i<31;i++)who[i]=u[i]; who[i]=0;
    if(!acct_is_admin(g_uid)){ sh_err(who," is not in the sudoers file (not an administrator). This incident will be reported."); acct_log("sudo: refused for ",who," (not an administrator)"); return 0; }
    if(g_ticks<g_sudo_until)return 1;
    if(!acct_has_password(who)){ tcol(rgb(255,205,100)); term_puts("sudo: no password is set for "); term_puts(who); term_puts(" - run passwd to protect administrator access\n"); tcol(WHT); g_sudo_until=g_ticks+(u64)g_sudo_min*6000u; return 1; }
    for(int t=0;t<3;t++){ char pw[72]; term_puts("[sudo] password for "); term_puts(who); term_puts(": "); if(sh_readpw(pw,72)<0)return 0;
        int ok=acct_check(who,pw); sh_wipe(pw,72);
        if(ok){ g_sudo_until=g_ticks+(u64)g_sudo_min*6000u; acct_log("sudo: accepted for ",who,0); return 1; }
        term_puts("Sorry, try again.\n"); acct_log("sudo: wrong password for ",who,0); sh_redraw(); }
    sh_err("sudo: 3 incorrect password attempts",0); return 0; }
void sh_sudo_reset(void){ g_sudo_until=0; }
static int sh_is_user_cmd(const char* c);
static void sh_user_cmds(const char* cmd,const char* arg);
static int need_root(const char* what){ if(g_uid==0)return 1; tcol(rgb(240,120,120)); term_puts(what); term_puts(": permission denied - administrator rights needed (try: sudo "); term_puts(what); term_puts(")\n"); tcol(WHT); return 0; }
int jc_sig_pending_cur(void){ return (g_jcur>=0)?(jc_deliverable(&g_jp[g_jcur])!=0):0; }
static int str_eq_n(const char*a,const char*b,int n){ for(int i=0;i<n;i++){ if(a[i]!=b[i])return 0; if(!a[i])return 1; } return 1; }

/* pipe-aware filters: read g_pipein, act, print (which may be captured by the pipeline). */
static int run_filter(const char* cmd,const char* arg){
    if(!g_pipein) return 0;
    if(str_eq_n(cmd,"grep",5)){
        const char* pat=arg; while(*pat==' ')pat++;
        const char* p=g_pipein;
        while(*p){ const char* ls=p; while(*p&&*p!='\n')p++; int llen=(int)(p-ls);
            /* substring match of pat in this line */
            int found=0; if(!*pat)found=1;
            for(const char* q=ls; q<=ls+llen-1 && !found; q++){ const char* a=q; const char* b=pat; while(*b&&a<ls+llen&&*a==*b){a++;b++;} if(!*b)found=1; }
            if(found){ for(int i=0;i<llen;i++)term_putc(ls[i]); term_putc('\n'); }
            if(*p=='\n')p++;
        }
        return 1;
    }
    if(str_eq_n(cmd,"wc",3)){
        int lines=0,words=0,chars=0,inw=0; for(const char* p=g_pipein;*p;p++){ chars++; if(*p=='\n')lines++; if(*p==' '||*p=='\n'||*p=='\t'){inw=0;} else if(!inw){inw=1;words++;} }
        char b[48],*o=b; o=scopy(o,"  lines ");o=udec(o,(u32)lines);o=scopy(o,"  words ");o=udec(o,(u32)words);o=scopy(o,"  chars ");o=udec(o,(u32)chars);*o=0; term_puts(b); term_putc('\n');
        return 1;
    }
    if(str_eq_n(cmd,"head",5)){
        int nl=10; const char* a=arg; while(*a==' ')a++; if(*a>='0'&&*a<='9'){nl=0;while(*a>='0'&&*a<='9'){nl=nl*10+(*a-'0');a++;}}
        const char* p=g_pipein; int c=0; while(*p&&c<nl){ const char* ls=p; while(*p&&*p!='\n')p++; for(const char* q=ls;q<p;q++)term_putc(*q); term_putc('\n'); if(*p=='\n')p++; c++; }
        return 1;
    }
    if(str_eq_n(cmd,"cat",4)){ term_puts(g_pipein); return 1; }   /* cat with pipe input just passes through */
    return 0;
}

static int sh_is_sh(const char* n){ int L=0; while(n[L])L++; return L>3&&n[L-3]=='.'&&n[L-2]=='s'&&n[L-1]=='h'; }
#define SHV_MAX 64
static char shv_name[SHV_MAX][32]; static char shv_val[SHV_MAX][192]; static int shv_n=0;
static int g_lastrc=0;
#define ALIAS_MAX 32
static char al_name[ALIAS_MAX][24]; static char al_val[ALIAS_MAX][96]; static int al_n=0;
static void al_set(const char* nm,const char* val){ for(int i=0;i<al_n;i++)if(seq(al_name[i],(char*)nm)){ int k=0; for(;val[k]&&k<95;k++)al_val[i][k]=val[k]; al_val[i][k]=0; return; } if(al_n>=ALIAS_MAX)return; int k=0; for(;nm[k]&&k<23;k++)al_name[al_n][k]=nm[k]; al_name[al_n][k]=0; k=0; for(;val[k]&&k<95;k++)al_val[al_n][k]=val[k]; al_val[al_n][k]=0; al_n++; }
static const char* al_get(const char* nm){ for(int i=0;i<al_n;i++)if(seq(al_name[i],(char*)nm))return al_val[i]; return 0; }
static const char* shv_get(const char* nm,int nl){ for(int i=0;i<shv_n;i++){ int j=0; for(;j<nl&&shv_name[i][j];j++)if(shv_name[i][j]!=nm[j])break; if(j==nl&&shv_name[i][j]==0)return shv_val[i]; } return 0; }
static void shv_set(const char* nm,const char* val){ for(int i=0;i<shv_n;i++){ if(seq(shv_name[i],(char*)nm)){ int k=0; for(;val[k]&&k<191;k++)shv_val[i][k]=val[k]; shv_val[i][k]=0; return; } } if(shv_n>=SHV_MAX)return; int k=0; for(;nm[k]&&k<31;k++)shv_name[shv_n][k]=nm[k]; shv_name[shv_n][k]=0; k=0; for(;val[k]&&k<191;k++)shv_val[shv_n][k]=val[k]; shv_val[shv_n][k]=0; shv_n++; }
static long sh_arith(const char* e){
    long terms[32]; char ops[32]; int nt=0; ops[0]='+'; int i=0;
    while(e[i]){ while(e[i]==' ')i++; long sign=1; if(e[i]=='-'){sign=-1;i++;} long num=0; while(e[i]>='0'&&e[i]<='9'){num=num*10+(e[i]-'0');i++;} num*=sign; if(nt<32)terms[nt++]=num; while(e[i]==' ')i++; char o=e[i]; if(o=='+'||o=='-'||o=='*'||o=='/'||o=='%'){ if(nt<32)ops[nt]=o; i++; } else break; }
    long v[32]; char vo[32]; int nv=0; v[0]=terms[0]; nv=1; vo[0]='+';
    for(int k=1;k<nt;k++){ char o=ops[k-1]; if(o=='*')v[nv-1]*=terms[k]; else if(o=='/'){if(terms[k])v[nv-1]/=terms[k];} else if(o=='%'){if(terms[k])v[nv-1]%=terms[k];} else { v[nv]=terms[k]; vo[nv]=o; nv++; } }
    long acc=v[0]; for(int k=1;k<nv;k++){ if(vo[k]=='-')acc-=v[k]; else acc+=v[k]; } return acc;
}

static void sh_capture(const char* cmd,char* out,int max){ int sav=g_capture,savn=g_cap_n; g_capture=1; g_cap_n=0; g_cap[0]=0; char tmp[512]; int k=0; for(;cmd[k]&&k<511;k++)tmp[k]=cmd[k]; tmp[k]=0; run_cmd(tmp); int n=0; for(;n<g_cap_n&&n<max-1;n++)out[n]=g_cap[n]; while(n>0&&(out[n-1]=='\n'||out[n-1]=='\r'))n--; out[n]=0; g_capture=sav; g_cap_n=savn; }
static void sh_expand(const char* in,char* out,int max){
    int o=0; int dq=0;
    for(int i=0;in[i]&&o<max-1;){
        if(in[i]=='"'){ dq=!dq; out[o++]=in[i++]; continue; }
        if(in[i]=='\''&&!dq){ out[o++]=in[i++]; while(in[i]&&in[i]!='\''&&o<max-1)out[o++]=in[i++]; if(in[i]&&o<max-1)out[o++]=in[i++]; continue; }
        if(in[i]=='~' && (i==0||in[i-1]==' ') && (in[i+1]=='/'||in[i+1]==0||in[i+1]==' ')){ const char* h=shv_get("HOME",4); if(h)for(int k=0;h[k]&&o<max-1;k++)out[o++]=h[k]; i++; continue; }
        if(in[i]=='$'){
            if(in[i+1]=='?'){ char b[12]; char* p=b; p=udec(p,(u32)g_lastrc); *p=0; for(int j=0;b[j]&&o<max-1;j++)out[o++]=b[j]; i+=2; continue; }
            if(in[i+1]=='('&&in[i+2]=='('){ int j=i+3; char ex2[128]; int s=0; int d2=2; while(in[j]&&s<127){ if(in[j]=='(')d2++; else if(in[j]==')'){d2--; if(d2==0)break;} ex2[s++]=in[j++]; } ex2[s]=0; if(in[j]==')')j++; if(in[j]==')')j++; char exx[160]; sh_expand(ex2,exx,160); long val=sh_arith(exx); char nb[24]; char* q=nb; if(val<0){*q++='-';val=-val;} q=udec(q,(u32)val); *q=0; for(int k=0;nb[k]&&o<max-1;k++)out[o++]=nb[k]; i=j; continue; }
            if(in[i+1]=='('){ int depth=1,j=i+2; char sub[256]; int s=0; while(in[j]&&depth&&s<255){ if(in[j]=='(')depth++; else if(in[j]==')'){depth--; if(!depth)break;} sub[s++]=in[j++]; } sub[s]=0; char cap[256]; sh_capture(sub,cap,256); for(int k=0;cap[k]&&o<max-1;k++)out[o++]=cap[k]; i=j+1; continue; }
            if(in[i+1]=='{'){ int j=i+2; char nm[32]; int n=0; while(in[j]&&in[j]!='}'&&n<31)nm[n++]=in[j++]; nm[n]=0; if(in[j]=='}')j++; const char* v=shv_get(nm,n); if(v)for(int k=0;v[k]&&o<max-1;k++)out[o++]=v[k]; i=j; continue; }
            if((in[i+1]>='A'&&in[i+1]<='Z')||(in[i+1]>='a'&&in[i+1]<='z')||in[i+1]=='_'){ int j=i+1; char nm[32]; int n=0; while(((in[j]>='A'&&in[j]<='Z')||(in[j]>='a'&&in[j]<='z')||(in[j]>='0'&&in[j]<='9')||in[j]=='_')&&n<31)nm[n++]=in[j++]; nm[n]=0; const char* v=shv_get(nm,n); if(v)for(int k=0;v[k]&&o<max-1;k++)out[o++]=v[k]; i=j; continue; }
        }
        out[o++]=in[i++];
    }
    out[o]=0;
}
static int sh_truth(const char* cond){
    char e[256]; sh_expand(cond,e,256); char* s=e; while(*s==' ')s++;
    if(s[0]=='['){ s++; while(*s==' ')s++; int L=0; while(s[L])L++; while(L>0&&(s[L-1]==' '||s[L-1]==']'))s[--L]=0; }
    else if(s[0]=='t'&&s[1]=='e'&&s[2]=='s'&&s[3]=='t'&&s[4]==' '){ s+=5; }
    else { char tmp[256]; int k=0; for(;s[k]&&k<255;k++)tmp[k]=s[k]; tmp[k]=0; run_cmd(tmp); return g_lastrc==0; }
    char a[96],op[8],b[96]; int ai=0,oi=0,bi=0; char* p=s;
    while(*p==' ')p++; while(*p&&*p!=' '&&ai<95)a[ai++]=*p++; a[ai]=0; while(*p==' ')p++;
    while(*p&&*p!=' '&&oi<7)op[oi++]=*p++; op[oi]=0; while(*p==' ')p++;
    while(*p&&bi<95)b[bi++]=*p++; b[bi]=0; { int L=bi; while(L>0&&b[L-1]==' ')b[--L]=0; }
    if(!op[0])return a[0]!=0;
    int na=atoi(a), nb=atoi(b);
    if(seq(op,"="))return seq(a,b); if(seq(op,"!="))return !seq(a,b);
    if(seq(op,"-eq"))return na==nb; if(seq(op,"-ne"))return na!=nb;
    if(seq(op,"-lt"))return na<nb; if(seq(op,"-gt"))return na>nb;
    if(seq(op,"-le"))return na<=nb; if(seq(op,"-ge"))return na>=nb;
    return 0;
}

static void sh_run_block(char** lines,int lo,int hi){
    int i=lo;
    while(i<hi){
        char* ln=lines[i]; while(*ln==' '||*ln=='\t')ln++;
        if(!ln[0]||ln[0]=='#'){ i++; continue; }
        if(str_eq_n(ln,"if ",3)){
            int depth=1, elseline=-1, fi=-1;
            for(int j=i+1;j<hi;j++){ char* t=lines[j]; while(*t==' '||*t=='\t')t++; if(str_eq_n(t,"if ",3))depth++; else if(seq(t,"fi")){ depth--; if(depth==0){fi=j;break;} } else if(depth==1&&seq(t,"else")&&elseline<0)elseline=j; }
            char cond[256]; { char* c=ln+3; int k=0; while(*c&&k<255){ if(str_eq_n(c,"; then",6)||(str_eq_n(c,"then",4)&&(c[4]==0||c[4]==' ')))break; cond[k++]=*c++; } cond[k]=0; }
            if(fi<0)fi=hi; int t=sh_truth(cond);
            if(t){ int bend=(elseline>=0)?elseline:fi; sh_run_block(lines,i+1,bend); }
            else if(elseline>=0){ sh_run_block(lines,elseline+1,fi); }
            i=fi+1; continue;
        }
        if(str_eq_n(ln,"while ",6)){
            int depth=1, done=-1;
            for(int j=i+1;j<hi;j++){ char* t=lines[j]; while(*t==' '||*t=='\t')t++; if(str_eq_n(t,"while ",6)||str_eq_n(t,"for ",4)||str_eq_n(t,"if ",3))depth++; else if(seq(t,"done")){ depth--; if(depth==0){done=j;break;} } else if(seq(t,"fi")){depth--;} }
            if(done<0)done=hi; char cond[256]; { char* c=ln+6; int k=0; while(*c&&k<255){ if(str_eq_n(c,"; do",4)||(str_eq_n(c,"do",2)&&(c[2]==0||c[2]==' ')))break; cond[k++]=*c++; } cond[k]=0; }
            int guard=0; while(sh_truth(cond)&&guard++<1000000){ sh_run_block(lines,i+1,done); } i=done+1; continue;
        }
        if(str_eq_n(ln,"for ",4)){
            int depth=1, done=-1;
            for(int j=i+1;j<hi;j++){ char* t=lines[j]; while(*t==' '||*t=='\t')t++; if(str_eq_n(t,"for ",4)||str_eq_n(t,"while ",6)||str_eq_n(t,"if ",3))depth++; else if(seq(t,"done")){ depth--; if(depth==0){done=j;break;} } else if(seq(t,"fi")){depth--;} }
            if(done<0)done=hi; char* c=ln+4; while(*c==' ')c++; char var[32]; int vn=0; while(*c&&*c!=' '&&vn<31)var[vn++]=*c++; var[vn]=0; while(*c==' ')c++; if(str_eq_n(c,"in ",3))c+=3;
            char items[256]; sh_expand(c,items,256); char* p=items; int guard=0;
            while(*p&&guard++<4096){ while(*p==' ')p++; if(!*p||*p==';')break; char val[96]; int k=0; while(*p&&*p!=' '&&*p!=';'&&k<95)val[k++]=*p++; val[k]=0; shv_set(var,val); sh_run_block(lines,i+1,done); } i=done+1; continue;
        }
        { char buf[512]; int k=0; for(;ln[k]&&k<511;k++)buf[k]=ln[k]; buf[k]=0; sh_run_line(buf); }
        i++;
    }
}
void sh_run_line(char* ln){
    while(*ln==' ')ln++;
    if(str_eq_n(ln,"alias ",6)){ const char* a=ln+6; while(*a==' ')a++; char nm[24]; int k=0; while(*a&&*a!='='&&k<23)nm[k++]=*a++; nm[k]=0; if(*a=='='){ a++; if(*a=='"'||*a=='\'')a++; char v[96]; int j=0; while(*a&&*a!='"'&&*a!='\''&&j<95)v[j++]=*a++; v[j]=0; al_set(nm,v);} g_lastrc=0; return; }
    if(seq(ln,"alias")){ for(int i=0;i<al_n;i++){ term_puts("alias "); term_puts(al_name[i]); term_puts("='"); term_puts(al_val[i]); term_puts("'\n"); } g_lastrc=0; return; }
    { char fw[24]; int k=0; const char* p=ln; while(*p&&*p!=' '&&k<23)fw[k++]=*p++; fw[k]=0; const char* av=al_get(fw); if(av){ static char xl[512]; int o=0; const char* q=av; while(*q&&o<511)xl[o++]=*q++; while(*p&&o<511)xl[o++]=*p++; xl[o]=0; ln=xl; while(*ln==' ')ln++; } }
    int eq=-1; for(int i=0;ln[i]&&ln[i]!=' ';i++){ if(ln[i]=='='){eq=i;break;} }
    if(eq>0){ int ok=1; for(int i=0;i<eq;i++){ char ch=ln[i]; if(!((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_')){ok=0;break;} } if(ok){ char nm[32]; int k=0; for(;k<eq&&k<31;k++)nm[k]=ln[k]; nm[k]=0; char val[256]; sh_expand(ln+eq+1,val,256); shv_set(nm,val); g_lastrc=0; return; } }
    if(str_eq_n(ln,"export ",7)){ sh_run_line(ln+7); return; }
    char ex[512]; sh_expand(ln,ex,512); run_cmd(ex);
}
static char* g_sh_lines[256]; static char g_sh_buf[8192];
static void sh_run_script(const char* text){
    int n=0; int bl=0;
    for(int i=0;text[i]&&bl<8190;){ g_sh_lines[n++]=&g_sh_buf[bl]; while(text[i]&&text[i]!='\n'&&bl<8190){ if(text[i]==';'){ i++; break; } g_sh_buf[bl++]=text[i++]; } g_sh_buf[bl++]=0; if(text[i]=='\n')i++; if(n>=255)break; }
    sh_run_block(g_sh_lines,0,n);
}

static int x_needs(const char* l){
    while(*l==' ')l++; char w[48]; int k=0; while(l[k]&&l[k]!=' '&&k<47){ w[k]=l[k]; k++; } w[k]=0;
    const char* b=w; for(int i=0;w[i];i++) if(w[i]=='/')b=w+i+1;
    static const char* const xp[]={"idle","idle3","idle3.13","wish","xdemo","gtkdemo",0};
    for(int i=0;xp[i];i++) if(seq((char*)b,(char*)xp[i]))return 1;
    for(const char* q=l;*q;q++){ const char* t1="tkinter"; const char* t2="idlelib"; const char* t3="turtle"; int a=1,c=1,e=1; for(int j=0;t1[j];j++) if(q[j]!=t1[j]){a=0;break;} for(int j=0;t2[j];j++) if(q[j]!=t2[j]){c=0;break;} for(int j=0;t3[j];j++) if(q[j]!=t3[j]){e=0;break;} if(a||c||e)return 1; }
    return 0; }
static int x_starting; int x_spawned;
void x_ensure(void){ extern int g_xfb_live; extern int g_xover; if(g_xfb_live||x_spawned){ if(g_state==ST_DESKTOP&&!g_xover)open_win(W_XDISP); return; } x_spawned=1; x_starting=1; static char sx[]="startx"; run_cmd(sx); x_starting=0; }
void x_launch(const char* prog){ extern int g_xfb_live; int was=g_xfb_live; x_ensure(); static char b[600]; char* p=b;
    if(!was){ p=scopy(p,"bash -c 'for i in $(seq 100); do [ -e /tmp/Xvfb_screen0 ] && break; sleep 0.2; done; sleep 1; exec "); p=scopy(p,prog); p=scopy(p,"' > /tmp/xapp.log 2>&1 &"); }
    else { p=scopy(p,prog); p=scopy(p," > /tmp/xapp.log 2>&1 &"); }
    *p=0; x_starting=1; run_cmd(b); x_starting=0; }
void run_cmd(char* line){
    if(!g_capture&&!x_starting){ extern int xapps_is_gui(const char*); extern void xrun_launch(const char*); if(xapps_is_gui(line)){ xrun_launch(line); return; } }
    if(!g_capture&&!x_starting&&x_needs(line)){ extern int g_xfb_live; if(!g_xfb_live){ int L=0; while(line[L])L++; while(L>0&&(line[L-1]==' '||line[L-1]=='&'))L--; static char pl[400]; int k=0; for(;k<L&&k<399;k++)pl[k]=line[k]; pl[k]=0; tcol(GREY); term_puts("starting the X server first...\n"); tcol(WHT); x_launch(pl); return; } else if(g_state==ST_DESKTOP){ extern int g_xover; if(!g_xover) open_win(W_XDISP); } }
    if(!g_capture&&jc_launch(line))return;
    /* split on '|' into stages; handle > < >> on the last/first stage. */
    /* find redirection targets first (on the whole line, applied to last stage) */
    char work[512]; int wl=0; for(int i=0;line[i]&&wl<511;i++)work[wl++]=line[i]; work[wl]=0;

    /* pull out > outfile  and  < infile  and >> */
    char outfile[64]={0}; int append=0; char infile[64]={0};
    for(int i=0;work[i];i++){
        if(work[i]=='>'){ int ap=(work[i+1]=='>'); char* d=outfile; int j=i+(ap?2:1); while(work[j]==' ')j++; int k=0; while(work[j]&&work[j]!=' '&&work[j]!='|'&&k<63)d[k++]=work[j++]; d[k]=0; append=ap; work[i]=0; /* cut here */ }
    }
    for(int i=0;work[i];i++){
        if(work[i]=='<'){ char* d=infile; int j=i+1; while(work[j]==' ')j++; int k=0; while(work[j]&&work[j]!=' '&&work[j]!='|'&&k<63)d[k++]=work[j++]; d[k]=0; for(int m=i;work[m];m++)work[m]=work[m+1]; i--; }
    }

    /* split remaining 'work' on '|' */
    char* stages[8]; int ns=0; stages[ns++]=work;
    for(int i=0;work[i]&&ns<8;i++){ if(work[i]=='|'){ work[i]=0; stages[ns++]=&work[i+1]; } }

    static char pbuf[8192];
    const char* feed=0;
    /* if input redirect, load file into pbuf as the first stage's stdin */
    if(infile[0]){ int fl=fat_read_file(infile,(u8*)pbuf,8191); if(fl<0)fl=0; pbuf[fl]=0; feed=pbuf; }
    if(!feed&&g_run_feed){ feed=g_run_feed; g_run_feed=0; }

    for(int st=0; st<ns; st++){
        char* stg=stages[st]; while(*stg==' ')stg++;
        /* trim trailing spaces */
        int L=0; while(stg[L])L++; while(L>0&&stg[L-1]==' ')stg[--L]=0;
        /* parse cmd + arg */
        char c2[24]; int ci=0; const char* a=stg; while(*a&&*a!=' '&&ci<23)c2[ci++]=*a++; c2[ci]=0; while(*a==' ')a++;

        int last=(st==ns-1);
        int capture_this = (!last) || outfile[0];   /* capture unless it's the final on-screen stage */

        g_pipein = feed;
        if(capture_this){ g_capture=1; g_cap_n=0; g_cap[0]=0; }

        /* run: try pipe-aware filter first (if it has pipe input), else the normal command */
        int handled = (feed && run_filter(c2,a));
        if(!handled) run_cmd_one(stg);

        if(capture_this){ g_capture=0;
            /* this stage's output becomes next stage's input */
            for(int i=0;i<=g_cap_n && i<8191;i++)pbuf[i]=g_cap[i];
            feed=pbuf;
        } else {
            feed=0;
        }
        g_pipein=0;
    }

    /* write captured output to a file if redirected */
    if(outfile[0]){
        { int ofd=vfs_open(outfile,O_WRONLY|O_CREAT|(append?O_APPEND:O_TRUNC)); if(ofd>=0){ vfs_write(ofd,pbuf,(int)slen(pbuf)); vfs_close(ofd); } else { sh_err("sh: cannot write to ",outfile); return; } }
        char b[80],*o=b; o=scopy(o,append?"  appended ":"  wrote ");o=udec(o,(u32)slen(pbuf));o=scopy(o," bytes to ");o=scopy(o,outfile);*o=0; term_puts(b); term_putc('\n');
    }
}
static char* acx64(char* p,u64 v){ p=scopy(p,"0x"); if(v>>32)p=hx(p,(u32)(v>>32),8); return hx(p,(u32)v,(v>>32)?8:(v>0xFFFF?8:(v>0xFF?4:2))); }
static void acpi_put_obj(const aobj* o,int ind,int* lines){
    if(*lines>=60){ if(*lines==60)term_puts("  ...\n"); (*lines)++; return; } (*lines)++;
    char b[200],*p=b; for(int i=0;i<ind&&i<20;i++)*p++=' ';
    if(o->type==AT_INT){ p=acx64(p,o->i); if(o->i<100000000ULL){ p=scopy(p,"  ("); p=udec(p,(u32)o->i); *p++=')'; } }
    else if(o->type==AT_STR){ *p++='"'; for(u32 k=0;k<o->len&&k<120;k++){ char c=(char)o->p[k]; *p++=(c>=32&&c<127)?c:'?'; } *p++='"'; }
    else if(o->type==AT_BUF){ p=scopy(p,"Buffer("); p=udec(p,o->len); p=scopy(p,"):"); for(u32 k=0;k<o->len&&k<32;k++){ *p++=' '; p=hx(p,o->p[k],2); } if(o->len>32)p=scopy(p," ..."); }
    else if(o->type==AT_PKG){ p=scopy(p,"Package("); p=udec(p,o->len); *p++=')'; *p++='\n'; *p=0; term_puts(b); for(u32 k=0;k<o->len;k++)acpi_put_obj(&o->e[k],ind+2,lines); return; }
    else if(o->type==AT_REF){ p=scopy(p,"Reference"); if(o->rk==RK_NODE&&o->node){ char path[80]; aml_path(o->node,path,80); p=scopy(p," -> "); p=scopy(p,path); } }
    else if(o->type==AT_NAMESTR){ p=scopy(p,"Name "); for(u32 k=0;k<o->len&&k<60;k++)*p++=(char)o->p[k]; }
    else p=scopy(p,"(nothing)");
    *p++='\n'; *p=0; term_puts(b); }
static const char* acpi_kind_name(int k){ switch(k){ case K_SCOPE: return "scope"; case K_DEVICE: return "device"; case K_NAME: return "name"; case K_METHOD: return "method"; case K_REGION: return "region"; case K_FIELD: return "field";
    case K_INDEXFIELD: return "indexfield"; case K_BANKFIELD: return "bankfield"; case K_BUFFIELD: return "bufferfield"; case K_MUTEX: return "mutex"; case K_EVENT: return "event"; case K_PROCESSOR: return "processor"; case K_POWER: return "powerres"; case K_THERMAL: return "thermalzone"; case K_ALIAS: return "alias"; case K_NATIVE: return "builtin"; } return "?"; }
static u64 acpi_parse_num(const char** sp){ const char* s=*sp; u64 v=0; if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')){ s+=2; for(;;){ char c=*s; int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; if(d<0)break; v=v*16+(u64)d; s++; } }
    else while(*s>='0'&&*s<='9'){ v=v*10+(u64)(*s-'0'); s++; } *sp=s; return v; }
static void acpi_cmd(const char* arg){
    char b[160],*p;
    if(!g_acs.up){ tcol(RED); term_puts("ACPI AML is not running (no FADT/DSDT found or out of memory).\n"); tcol(WHT); return; }
    if(arg[0]=='e'&&arg[1]=='v'){ const char* a=arg; while(*a&&*a!=' ')a++; while(*a==' ')a++; char path[80]; int n=0; while(*a&&*a!=' '&&n<79)path[n++]=*a++; path[n]=0;
        if(!n){ term_puts("usage: acpi eval \\_SB.PCI0.LPCB.EC0.BAT0._BST [args]\n"); return; }
        anode* m=aml_find(path); if(!m){ tcol(RED); term_puts("no such object\n"); tcol(WHT); return; }
        aobj args[7]; int na=0; while(na<7){ while(*a==' ')a++; if(!*a)break; args[na].type=AT_NONE; args[na].p=0; args[na].e=0; args[na].own=0; aobj_int(&args[na],acpi_parse_num(&a)); na++; while(*a&&*a!=' ')a++; }
        aobj r; g_aml_lasterr[0]=0; int rc=aml_eval(m,args,na,&r); for(int z=0;z<na;z++)aobj_free(&args[z]);
        if(rc){ tcol(RED); p=b; p=scopy(p,"evaluation failed: "); p=scopy(p,g_aml_lasterr[0]?g_aml_lasterr:"error"); *p++='\n'; *p=0; term_puts(b); tcol(WHT); aobj_free(&r); return; }
        if(r.type==AT_REF){ aobj d; d.type=AT_NONE; d.p=0; d.e=0; d.own=0; d.len=0; aml_deref(&r,&d); aobj_free(&r); r=d; }
        int lines=0; tcol(GREEN); acpi_put_obj(&r,0,&lines); tcol(WHT); aobj_free(&r); acpi_drain_notify(); return; }
    if(arg[0]=='l'&&arg[1]=='s'){ const char* a=arg+2; while(*a==' ')a++; anode* n=*a?aml_find(a):g_aroot; if(!n){ tcol(RED); term_puts("no such object\n"); tcol(WHT); return; }
        int c=0; for(anode* x=n->child;x&&c<80;x=x->next,c++){ p=b; p=scopy(p,"  "); for(int k=0;k<4;k++)*p++=x->nm[k]; p=scopy(p,"  "); p=scopy(p,acpi_kind_name(x->kind));
            if(x->kind==K_METHOD){ p=scopy(p,"("); p=udec(p,x->argc); p=scopy(p," args)"); } if(x->kind==K_REGION){ p=scopy(p," space "); p=udec(p,x->space); p=scopy(p," at "); p=acx64(p,x->base); }
            *p++='\n'; *p=0; term_puts(b); }
        if(!c)term_puts("  (empty)\n"); return; }
    if(arg[0]=='e'&&arg[1]=='c'){ if(!g_aec.ok){ term_puts("no embedded controller\n"); return; }
        for(int r=0;r<16;r++){ p=b; p=hx(p,(u32)r*16,2); *p++=':'; for(int c=0;c<16;c++){ u8 v=0; *p++=' '; if(aec_read((u8)(r*16+c),&v)){ *p++='?'; *p++='?'; } else p=hx(p,v,2); } *p++='\n'; *p=0; term_puts(b); } return; }
    tcol(CYAN); term_puts("ACPI\n"); tcol(WHT);
    p=b; p=scopy(p,"  tables "); p=udec(p,(u32)g_acs.tables); if(g_acs.bad_tables){ p=scopy(p," ("); p=udec(p,(u32)g_acs.bad_tables); p=scopy(p," with parse errors)"); } p=scopy(p,", "); p=udec(p,g_aml_nodes); p=scopy(p," objects, "); p=udec(p,g_aml_loaded_bytes/1024); p=scopy(p," KB AML, set-up "); p=udec(p,g_acs.ms); p=scopy(p," ms\n"); *p=0; term_puts(b);
    p=b; p=scopy(p,"  ACPI mode "); p=scopy(p,g_acs.acpi_mode?"on":"off"); p=scopy(p,", AML errors "); p=udec(p,(u32)g_aml_err); if(g_acs.errs[0]){ p=scopy(p," (first run: "); p=scopy(p,g_acs.errs); *p++=')'; } *p++='\n'; *p=0; term_puts(b);
    p=b; p=scopy(p,"  EC "); if(g_aec.ok){ p=acx64(p,g_aec.data); *p++='/'; p=acx64(p,g_aec.cmd); p=scopy(p,g_aec.from_ecdt?" (ECDT)":" (namespace)"); p=scopy(p,", GPE "); p=udec(p,g_aec.gpe); p=scopy(p,", events "); p=udec(p,g_aec.events);
        if(g_aec.events){ p=scopy(p," (last _Q"); p=hx(p,g_aec.lastq,2); *p++=')'; } p=scopy(p,", timeouts "); p=udec(p,g_aec.timeouts); } else p=scopy(p,"not found"); *p++='\n'; *p=0; term_puts(b);
    p=b; p=scopy(p,"  batteries "); p=udec(p,(u32)g_acb_n); p=scopy(p,", AC adapter "); p=scopy(p,!g_acad?"none":g_ac_online==1?"online":g_ac_online==0?"offline":"unknown"); *p++='\n'; *p=0; term_puts(b);
    p=b; p=scopy(p,"  backlight "); if(g_bl.ok){ p=scopy(p,g_bl.mode==1?"ACPI _BCM":g_bl.mode==2?"Intel PWM":"ACPI _BCM + Intel PWM"); p=scopy(p,", "); p=udec(p,(u32)g_bl.cur); p=scopy(p,"%"); if(g_bl.out){ char path[80]; aml_path(g_bl.out,path,80); p=scopy(p,", "); p=scopy(p,path); } } else p=scopy(p,"not found"); *p++='\n'; *p=0; term_puts(b);
    p=b; p=scopy(p,"  notifications "); p=udec(p,g_acs.notifies); p=scopy(p," ("); p=udec(p,g_acs.unhandled); p=scopy(p," not handled), blocked RAM writes "); p=udec(p,g_acpi_memblock); *p++='\n'; *p=0; term_puts(b);
    term_puts("  acpi ls [path] | acpi eval <path> [args] | acpi ec\n"); }
