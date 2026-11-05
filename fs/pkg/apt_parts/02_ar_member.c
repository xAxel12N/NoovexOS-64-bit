static int ar_member(const u8* a,long n,const char* pfx,const u8** out,long* olen){
    if(n<8||a[0]!='!'||a[1]!='<'||a[2]!='a'||a[3]!='r'||a[4]!='c'||a[5]!='h'||a[6]!='>')return 0;
    long off=8;
    while(off+60<=n){ const char* h=(const char*)(a+off); long sz=0; for(int i=48;i<58&&ap_dig(h[i]);i++)sz=sz*10+(h[i]-'0');
        int m=1; for(int i=0;pfx[i];i++) if(h[i]!=pfx[i]){ m=0; break; }
        if(m&&off+60+sz<=n){ *out=a+off+60; *olen=sz; return 1; }
        off+=60+sz+(sz&1); }
    return 0; }
__attribute__((unused)) static u8* build_layer(long* outn){
    long strn=1; for(int i=0;i<g_nle;i++)strn+=asl(g_le[i].name)+1+asl(g_le[i].tgt)+1;
    long hdr=16+16+(long)g_nle*24; long so=hdr; long doff=(so+strn+15)&~15L; long tot=doff+g_nld;
    u8* b=(u8*)kmalloc((unsigned long)tot); if(!b)return 0; for(long i=0;i<doff;i++)b[i]=0;
    b[0]='N'; b[1]='V'; b[2]='S'; b[3]='2'; *(u32*)(b+4)=(u32)g_nle; *(u32*)(b+8)=1; *(u32*)(b+12)=(u32)so;
    *(u32*)(b+16)=0; *(u32*)(b+20)=(u32)doff; *(u32*)(b+24)=(u32)g_nld; *(u32*)(b+28)=(u32)g_nld;
    long sp=so+1;
    for(int i=0;i<g_nle;i++){ u32* r=(u32*)(b+32+(long)i*24); int nl=asl(g_le[i].name), tl=asl(g_le[i].tgt);
        r[0]=(u32)(sp-so); for(int k=0;k<=nl;k++)b[sp+k]=(u8)g_le[i].name[k]; sp+=nl+1;
        if(tl){ r[1]=(u32)(sp-so); for(int k=0;k<=tl;k++)b[sp+k]=(u8)g_le[i].tgt[k]; sp+=tl+1; } else r[1]=0;
        r[2]=g_le[i].mode; r[3]=0; r[4]=(u32)g_le[i].off; r[5]=(u32)g_le[i].len; }
    for(long i=0;i<g_nld;i++)b[doff+i]=g_ldata[i];
    *outn=tot; return b; }
static int ctl_field(const u8* c,long n,const char* key,char* out,int cap){
    int kl=asl(key); long i=0; out[0]=0;
    while(i<n){ long l=i; while(i<n&&c[i]!='\n')i++; long le=i; i++;
        if(le-l>kl&&c[l+kl]==':'){ int m=1; for(int k=0;k<kl;k++) if(c[l+k]!=(u8)key[k]){ m=0; break; } if(m){ long v=l+kl+1; while(v<le&&c[v]==' ')v++; int o=0; while(v<le&&o<cap-1)out[o++]=(char)c[v++]; out[o]=0; return 1; } } }
    return 0; }
static char g_post[1024]; static int g_postn;
static void post_add(const char* nm){ int L=asl(nm); if(g_postn+L+2>=(int)sizeof g_post)return; if(g_postn)g_post[g_postn++]=' '; for(int i=0;i<L;i++)g_post[g_postn++]=nm[i]; g_post[g_postn]=0; }
static char g_runp[2048]; static int g_runpn;
static void run_add(const char* nm){ int L=asl(nm); if(g_runpn+L+2>=(int)sizeof g_runp)return; if(g_runpn)g_runp[g_runpn++]=' '; for(int i=0;i<L;i++)g_runp[g_runpn++]=nm[i]; g_runp[g_runpn]=0; }
static int manifest_has(const char* pkg,const char* pre,int pl){
    char lf[128]; char* q=lf; q=sj(q,AP_PKGDIR "/"); q=sj(q,pkg); q=sj(q,".list"); long n=0; const u8* d=rfile(lf,&n); if(!d)return 0;
    long i=0; while(i<n){ long e=i; while(e<n&&d[e]!='\n')e++; if(e-i>=pl){ int m=1; for(int k=0;k<pl;k++) if(d[i+k]!=(u8)pre[k]){ m=0; break; } if(m&&(e-i==pl||d[i+pl]=='/'||pre[pl-1]=='/'))return 1; } i=e+1; }
    return 0; }
static char* trig_lines(char* q,char* end){
    for(int i=0;i<g_nin;i++){ char tf[128]; char* w=tf; w=sj(w,"/var/lib/dpkg/info/"); w=sj(w,g_in[i].name); w=sj(w,".triggers"); long n=0; const u8* d=rfile(tf,&n); if(!d)continue;
        long a=0; while(a<n){ long e=a; while(e<n&&d[e]!='\n')e++; char ln[200]; int L=0; for(long k=a;k<e&&L<199;k++)ln[L++]=(char)d[k]; ln[L]=0; a=e+1;
            const char* t=ln; while(*t==' ')t++; int isint=0; if(t[0]=='i'&&t[1]=='n'&&t[2]=='t'&&t[3]=='e'&&t[4]=='r'&&t[5]=='e'&&t[6]=='s'&&t[7]=='t'){ isint=1; t+=8; while(*t&&*t!=' ')t++; } if(!isint)continue; while(*t==' ')t++; if(*t!='/')continue;
            char path[160]; int pl=0; while(t[pl]&&t[pl]!=' '&&pl<159){ path[pl]=t[pl]; pl++; } path[pl]=0;
            const char* r=g_runp; int hit=0; while(*r&&!hit){ char nm[64]; int k=0; while(*r&&*r!=' '&&k<63)nm[k++]=*r++; nm[k]=0; while(*r==' ')r++; if(k&&manifest_has(nm,path,pl))hit=1; }
            if(hit&&q+pl+asl(g_in[i].name)+16<end){ q=sj(q,"trig "); q=sj(q,g_in[i].name); *q++=' '; q=sj(q,path); *q++='\n'; *q=0; } } }
    return q; }
static void post_run(void){
    if(!g_postn&&!g_runpn)return;
    static char sc[4096]; char* q=sc;
    q=sj(q,"trig() { f=/var/lib/dpkg/info/$1.postinst; [ -f \"$f\" ] || return 0; echo \"Processing triggers for $1 ...\"; DPKG_MAINTSCRIPT_NAME=postinst DPKG_MAINTSCRIPT_PACKAGE=$1 /bin/sh \"$f\" triggered \"$2\" </dev/null || echo \"  (the trigger of $1 ended with code $?)\"; }\n");
    q=sj(q,"set +e\nexport DEBIAN_FRONTEND=noninteractive PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/opt/gnu/bin\nfor p in "); q=sj(q,g_post);
    q=sj(q,"; do\n  f=/var/lib/dpkg/info/$p.postinst\n  [ -f \"$f\" ] || continue\n  echo \"Configuring $p ...\"\n  DPKG_MAINTSCRIPT_NAME=postinst DPKG_MAINTSCRIPT_PACKAGE=$p /bin/sh \"$f\" configure </dev/null || echo \"  (the setup script of $p ended with code $?; the package is installed)\"\ndone\n");
    q=trig_lines(q,sc+sizeof sc-64);
    g_runpn=0; g_runp[0]=0;
    wfile("/var/lib/dpkg/nvx-postinst.sh",(const u8*)sc,(long)(q-sc)); g_postn=0; g_post[0]=0;
    extern int jc_launch(const char*); jc_launch("/opt/gnu/bin/bash /var/lib/dpkg/nvx-postinst.sh"); }
static int deb_install(const u8* deb,long n,const char* expect,int autoi,int verbose_files){
    const u8* cm; long cl; const u8* dm; long dl;
    if(!ar_member(deb,n,"control.tar",&cm,&cl)||!ar_member(deb,n,"data.tar",&dm,&dl)){ APE("not a Debian package archive",0); return 0; }
    long ctl_n=0; u8* ctl=unp_auto(cm,cl,&ctl_n); if(!ctl){ APE("cannot decompress control.tar",0); return 0; }
    const u8* cf; long cfl; if(tar_scan(ctl,ctl_n,0,"./control",&cf,&cfl)!=1){ kfree(ctl); APE("package has no control file",0); return 0; }
    inst_t x; for(int i=0;i<(int)sizeof(x);i++)((char*)&x)[i]=0;
    ctl_field(cf,cfl,"Package",x.name,64); ctl_field(cf,cfl,"Version",x.ver,64); ctl_field(cf,cfl,"Architecture",x.arch,16); ctl_field(cf,cfl,"Description",x.desc,96); { char pd[128]; char dd[256]; ctl_field(cf,cfl,"Pre-Depends",pd,128); ctl_field(cf,cfl,"Depends",dd,256); char* p=x.dep; if(pd[0]){ p=sj(p,pd); if(dd[0])p=sj(p,", "); } if(asl(x.dep)+asl(dd)<255)sj(p,dd); }
    const u8* ps; long psl; int has_post=tar_scan(ctl,ctl_n,0,"./postinst",&ps,&psl)==1;
    { const u8* tg; long tgl; if(x.name[0]&&tar_scan(ctl,ctl_n,0,"./triggers",&tg,&tgl)==1){ char tf[128]; char* q=tf; q=sj(q,"/var/lib/dpkg/info/"); q=sj(q,x.name); q=sj(q,".triggers"); mkdirp("/var/lib/dpkg/info"); wfile(tf,tg,tgl); } }
    if(x.name[0])run_add(x.name);
    if(has_post&&x.name[0]){ char pf[128]; char* q=pf; q=sj(q,"/var/lib/dpkg/info/"); q=sj(q,x.name); q=sj(q,".postinst"); mkdirp("/var/lib/dpkg/info"); wfile(pf,ps,psl); post_add(x.name); }
    kfree(ctl);
    if(!x.name[0]){ APE("package has no name",0); return 0; }
    if(expect&&expect[0]){ int m=1; for(int i=0;;i++){ if(expect[i]!=x.name[i]){ m=0; break; } if(!expect[i])break; } if(!m)APW("package name differs from index: ",x.name); }
    if(x.arch[0]&&!seq(x.arch,"amd64")&&!seq(x.arch,"all")){ APE("wrong architecture: ",x.arch); return 0; }
    AP("Unpacking "); AP(x.name); AP(" ("); AP(x.ver); APL(") ...");
    long dn=0; u8* data=unp_auto(dm,dl,&dn); if(!data){ APE("cannot decompress data.tar (needs gzip, xz or zstd)",0); return 0; }
    extern int g_fs_batch; extern volatile int g_frz_hold; extern void nvx3_sync_all(void); int ofh=g_frz_hold; g_frz_hold=1; g_fs_batch=1; u64 t0=g_ticks;
    man_reset(); int cnt=tar_scan(data,dn,2,0,0,0); kfree(data);
    u64 t1=g_ticks; g_fs_batch=0; nvx3_sync_all(); g_frz_hold=ofh;
    { char kb[160],*q=kb; q=sj(q,"apt: unpacked "); q=sj(q,x.name); q=sj(q,", "); q=udec(q,(u32)(cnt<0?0:cnt)); q=sj(q," files in "); q=udec(q,(u32)((t1-t0)*10)); q=sj(q," ms, saved to disk in "); q=udec(q,(u32)((g_ticks-t1)*10)); q=sj(q," ms"); *q=0; klog(kb); }
    if(cnt<0){ APE("out of space or memory while unpacking",0); return 0; }
    if(verbose_files){ long shown=0,i=0; while(i<g_man_n&&shown<12){ long e=i; while(e<g_man_n&&g_man[e]!='\n')e++; char lb[160]; long c=e-i<159?e-i:159; for(long k=0;k<c;k++)lb[k]=g_man[i+k]; lb[c]=0; tcol(GREY); AP("  "); AP(lb); term_putc('\n'); tcol(WHT); i=e+1; shown++; } if(g_man_files>12){ tcol(GREY); AP("  ... "); APN(g_man_files-12); APL(" more"); tcol(WHT); } }
    char fn[128]; char* p=fn; p=sj(p,AP_PKGDIR "/"); p=sj(p,x.name); p=sj(p,".list");
    wfile(fn,(const u8*)(g_man?g_man:(const char*)""),g_man_n); man_reset(); { extern int g_apps_gen; g_apps_gen++; }
    int si=st_find(svs(x.name)); if(si>=0){ int keep=g_in[si].autoi&&autoi; g_in[si]=x; g_in[si].autoi=keep; }
    else { if(g_nin>=g_cin){ int nc=g_cin?g_cin*2:64; inst_t* y=(inst_t*)krealloc(g_in,sizeof(inst_t)*(unsigned long)nc); if(!y)return 0; g_in=y; g_cin=nc; } x.autoi=autoi; g_in[g_nin++]=x; }
    st_save();
    AP("Setting up "); AP(x.name); AP(" ("); AP(x.ver); APL(") ...");
    if(has_post){ (void)ps; (void)psl; }
    return 1; }

static void apt_update(void){
    src_load(); idx_free(); int ok=0, hit=1;
    for(int i=0;i<g_nsrc;i++){ char base[200]; int flat=0; { int k=0; for(;g_srcbase[i][k]&&k<199;k++){ if(g_srcbase[i][k]=='|'){ flat=1; break; } base[k]=g_srcbase[i][k]; } base[k]=0; }
        static const char* const ext[]={".xz",".gz","",0}; u8* got=0; long gl=0; char url[400];
        for(int e=0;ext[e]&&!got;e++){ char* p=url; p=sj(p,base); if(!flat)p=sj(p,"/binary-amd64"); p=sj(p,"/Packages"); p=sj(p,ext[e]);
            if(e==0){ AP("Get:"); APN(hit); AP(" "); AP(base); APL(flat?" Packages":" amd64 Packages"); }
            got=ap_get(url,&gl,0); }
        if(!got){ tcol(rgb(240,120,120)); AP("Err:"); APN(hit); AP(" "); AP(base); AP("  "); APL(g_fetch_err?g_fetch_err:"failed"); tcol(WHT); hit++; continue; }
        long hl=5+asl(g_srcbase[i])+1; u8* f=(u8*)kmalloc((unsigned long)(gl+hl)); if(!f){ kfree(got); continue; }
        char* p=(char*)f; p=sj(p,"NVXL "); p=sj(p,g_srcbase[i]); *p++='\n'; for(long k=0;k<gl;k++)f[hl+k]=got[k]; kfree(got);
        char fn[96]; p=fn; p=sj(p,AP_LISTDIR "/src"); p=udec(p,(u32)i); p=sj(p,".pkgs"); *p=0;
        wfile(fn,f,gl+hl); kfree(f); tcol(GREY); AP("Fetched "); Psize(gl); term_putc('\n'); tcol(WHT); sh_flush(); ok++; hit++; }
    AP("Reading package lists... "); sh_flush(); int n=idx_load(1); APL("Done");
    if(!ok){ APE("no package lists could be downloaded - check the network and " AP_SOURCES,0); return; }
    st_load(); int up=0; for(int i=0;i<g_nin;i++){ int r=idx_best(svs(g_in[i].name)); if(r>=0&&vcmp(g_pr[r].ver,svs(g_in[i].ver))>0)up++; }
    APN(n); AP(" packages available. "); if(up){ APN(up); APL(" packages can be upgraded. Run 'apt list --upgradable' to see them."); } else APL("All packages are up to date."); }

static int g_yes;
static void plan_print(int nreq){
    long dl=0, isz=0; int nnew=0;
    for(int i=0;i<g_nplan;i++){ prec* r=&g_pr[g_plan[i]]; dl+=r->size; long z=0; for(int k=0;k<r->isize.n&&ap_dig(r->isize.p[k]);k++)z=z*10+(r->isize.p[k]-'0'); isz+=z*1024; if(st_find(r->name)<0)nnew++; }
    if(g_nplan>nreq){ APL("The following additional packages will be installed:"); int col=0; AP(" "); for(int i=0;i<g_nplan;i++){ if(!g_planauto[i])continue; prec* r=&g_pr[g_plan[i]]; if(col+r->name.n+1>96){ term_puts("\n "); col=0; } term_putc(' '); APSV(r->name); col+=r->name.n+1; } term_putc('\n'); }
    APL(nnew?"The following NEW packages will be installed:":"The following packages will be upgraded:"); { int col=0; AP(" "); for(int i=0;i<g_nplan;i++){ prec* r=&g_pr[g_plan[i]]; if(col+r->name.n+1>96){ term_puts("\n "); col=0; } term_putc(' '); APSV(r->name); col+=r->name.n+1; } term_putc('\n'); }
    APN(g_nplan-nnew); AP(" upgraded, "); APN(nnew); APL(" newly installed, 0 to remove and 0 not upgraded.");
    AP("Need to get "); Psize(dl); APL(" of archives.");
    AP("After this operation, "); Psize(isz); APL(" of additional disk space will be used."); }
static int elf_soname(const u8* d,long n,char* out,int cap){
    if(n<64||d[0]!=0x7F||d[1]!='E'||d[2]!='L'||d[3]!='F'||d[4]!=2)return 0;
    u64 phoff=*(const u64*)(d+32); u16 phes=*(const u16*)(d+54), phn=*(const u16*)(d+56);
    if(phes<56||phoff>=(u64)n||phoff+(u64)phes*phn>(u64)n)return 0;
    u64 doff=0,dsz=0; for(int i=0;i<phn;i++){ const u8* ph=d+phoff+(u64)i*phes; if(*(const u32*)ph==2){ doff=*(const u64*)(ph+8); dsz=*(const u64*)(ph+32); } }
    if(!dsz||doff>=(u64)n||dsz>(u64)n-doff)return 0;
    u64 strva=0, son=~0ULL; for(u64 q=0;q+16<=dsz;q+=16){ long t=*(const long*)(d+doff+q); u64 v=*(const u64*)(d+doff+q+8); if(!t)break; if(t==5)strva=v; else if(t==14)son=v; }
    if(son==~0ULL||!strva)return 0;
    u64 so=~0ULL; for(int i=0;i<phn;i++){ const u8* ph=d+phoff+(u64)i*phes; if(*(const u32*)ph!=1)continue; u64 off=*(const u64*)(ph+8), va=*(const u64*)(ph+16), fs=*(const u64*)(ph+32); if(strva>=va&&strva<va+fs){ so=off+(strva-va); break; } }
    if(so==~0ULL||so+son>=(u64)n)return 0;
    int k=0; for(u64 p=so+son;p<(u64)n&&d[p]&&k<cap-1;p++)out[k++]=(char)d[p]; out[k]=0; return k>0; }
static int ld_libcmp(const char* p1,const char* p2){
    while(*p1){ if(*p1>='0'&&*p1<='9'){ if(*p2>='0'&&*p2<='9'){ int v1=*p1++-'0', v2=*p2++-'0'; while(*p1>='0'&&*p1<='9')v1=v1*10+(*p1++-'0'); while(*p2>='0'&&*p2<='9')v2=v2*10+(*p2++-'0'); if(v1!=v2)return v1-v2; } else return 1; }
        else if(*p2>='0'&&*p2<='9')return -1; else if(*p1!=*p2)return *p1-*p2; else { p1++; p2++; } }
    return *p1-*p2; }
typedef struct { char key[96]; char val[200]; } ldce;
static int ld_dir_ok(const char* d,int n){ static const char* const D[]={"/usr/lib/x86_64-linux-gnu","/lib/x86_64-linux-gnu","/usr/lib","/lib","/usr/local/lib","/usr/lib64",0};
    for(int i=0;D[i];i++){ int L=asl(D[i]); if(L!=n)continue; int eq=1; for(int k=0;k<L;k++) if(d[k]!=D[i][k]){ eq=0; break; } if(eq)return 1; } return 0; }
static void ld_glibc_switch(const char* lst,long ln){
    long i=0; while(i<ln){ long e=i; while(e<ln&&lst[e]!='\n')e++; long c=e-i; const char* t="/ld-linux-x86-64.so.2"; int tl=asl(t);
        if(c>tl&&c<250){ int m=1; for(int k=0;k<tl;k++) if(lst[e-tl+k]!=t[k]){ m=0; break; }
            if(m){ char pb[256]; for(long k=0;k<c;k++)pb[k]=lst[i+k]; pb[c]=0; int isd=0; int f=ux_lookup(pb,&isd,1);
                if(f>=0&&!isd&&FS[f].len>4096){ const u8* d=(const u8*)fsd(f); if(d[0]==0x7F&&d[1]=='E'){ long n=FS[f].len; u8* cp=(u8*)kmalloc((unsigned long)n); if(cp){ for(long k=0;k<n;k++)cp[k]=d[k]; if(wfile("/lib64/ld-linux-x86-64.so.2",cp,n))ux_chmod("/lib64/ld-linux-x86-64.so.2",0100755u); kfree(cp); } return; } } } }
        i=e+1; } }
static void ld_cache_build(void){
    int cap=4096; ldce* E=(ldce*)kmalloc(sizeof(ldce)*(unsigned long)cap); if(!E)return; int ne=0; int glibc=0;
    char nm[64]; int ty=0;
    for(int li=0;k_readdir_at(AP_PKGDIR,li,nm,&ty)==1&&li<8192;li++){ int L=asl(nm); if(L<6||nm[L-5]!='.'||nm[L-4]!='l'||nm[L-3]!='i'||nm[L-2]!='s'||nm[L-1]!='t')continue;
        char fn[128]; char* p=fn; p=sj(p,AP_PKGDIR "/"); p=sj(p,nm); long mn=0; const u8* md=rfile(fn,&mn); if(!md)continue;
        u8* mc=(u8*)kmalloc((unsigned long)(mn?mn:1)); if(!mc)continue; for(long i=0;i<mn;i++)mc[i]=md[i];
        if(L==10&&nm[0]=='l'&&nm[1]=='i'&&nm[2]=='b'&&nm[3]=='c'&&nm[4]=='6'){ ld_glibc_switch((const char*)mc,mn); glibc=1; }
        long i=0; while(i<mn&&ne<cap){ long e=i; while(e<mn&&mc[e]!='\n')e++; long c=e-i; char pb[256];
            if(c>0&&c<255){ for(long k=0;k<c;k++)pb[k]=(char)mc[i+k]; pb[c]=0; int sl=(int)c; while(sl>0&&pb[sl-1]!='/')sl--; const char* b=pb+sl;
                int lib=b[0]=='l'&&b[1]=='i'&&b[2]=='b'; int hasso=0; for(int k=0;b[k];k++) if(b[k]=='.'&&b[k+1]=='s'&&b[k+2]=='o'&&b[k+3]=='.')hasso=1;
                if(lib&&hasso&&sl>1&&ld_dir_ok(pb,sl-1)&&asl(b)<95){ int isd=0; int f=ux_lookup(pb,&isd,1);
                    if(f>=0&&!isd){ char so[96]; if(elf_soname((const u8*)fsd(f),FS[f].len,so,96)&&seq(so,b)){
                        int dup=-1; for(int q=0;q<ne;q++) if(seq(E[q].key,b)){ dup=q; break; }
                        int slot=dup>=0?dup:ne++; int k=0; for(;b[k];k++)E[slot].key[k]=b[k]; E[slot].key[k]=0; k=0; for(;pb[k]&&k<199;k++)E[slot].val[k]=pb[k]; E[slot].val[k]=0; } } } }
            i=e+1; }
        kfree(mc); }
    if(!glibc){ int isd=0; if(ux_lookup("/lib64/ld-linux-x86-64.so.2",&isd,0)>=0)k_unlink("/lib64/ld-linux-x86-64.so.2"); }
    if(!ne){ kfree(E); int isd=0; if(ux_lookup("/etc/ld.so.cache",&isd,0)>=0)k_unlink("/etc/ld.so.cache"); return; }
    for(int a=1;a<ne;a++){ ldce t=E[a]; int b=a-1; while(b>=0&&ld_libcmp(E[b].key,t.key)<0){ E[b+1]=E[b]; b--; } E[b+1]=t; }
    long strs=0; for(int q=0;q<ne;q++)strs+=asl(E[q].key)+1+asl(E[q].val)+1;
    long hdr=48, tot=hdr+(long)ne*24+strs; u8* o=(u8*)kmalloc((unsigned long)tot); if(!o){ kfree(E); return; } for(long q=0;q<tot;q++)o[q]=0;
    const char* mg="glibc-ld.so.cache1.1"; for(int q=0;q<20;q++)o[q]=(u8)mg[q];
    *(u32*)(o+20)=(u32)ne; *(u32*)(o+24)=(u32)strs; o[28]=2;
    long sp=hdr+(long)ne*24;
    for(int q=0;q<ne;q++){ u8* en=o+hdr+(long)q*24; *(int*)en=0x0303; long ko=sp; int kl=asl(E[q].key); for(int k=0;k<=kl;k++)o[sp++]=(u8)E[q].key[k]; long vo=sp; int vl=asl(E[q].val); for(int k=0;k<=vl;k++)o[sp++]=(u8)E[q].val[k]; *(u32*)(en+4)=(u32)ko; *(u32*)(en+8)=(u32)vo; }
    wfile("/etc/ld.so.cache",o,tot); kfree(o); kfree(E); }
static int plan_run2(void);
static int plan_run(void){ extern volatile int g_frz_hold; int ofh=g_frz_hold; g_frz_hold=1; int r=plan_run2(); g_frz_hold=ofh; return r; }
static int plan_run2(void){
    int done=0;
    for(int i=0;i<g_nplan;i++){ prec* r=&g_pr[g_plan[i]]; char base[200]; { const char* sb=g_srcbase[r->src]; int k=0; int ds=-1; for(int q=0;sb[q];q++) if(sb[q]=='d'&&sb[q+1]=='i'&&sb[q+2]=='s'&&sb[q+3]=='t'&&sb[q+4]=='s'&&sb[q+5]=='/'){ ds=q; break; }
            if(ds<0){ for(;sb[k]&&sb[k]!='|'&&k<199;k++)base[k]=sb[k]; } else for(;k<ds&&k<199;k++)base[k]=sb[k]; base[k]=0; }
        char url[600]; char* p=url; p=sj(p,base); { char f[400]; svcpy(f,r->file,400); p=sj(p,f); }
        AP("Get:"); APN(i+1); AP(" "); AP(base); AP(" "); APSV(r->name); AP(" "); APSV(r->arch); AP(" "); APSV(r->ver); AP(" ["); Psize(r->size); APL("]");
        long n=0; u8* deb=ap_get(url,&n,r->size);
        if(!deb){ tcol(rgb(240,120,120)); AP("Err:"); APN(i+1); AP(" "); AP(url); AP("  "); APL(g_fetch_err?g_fetch_err:"failed"); tcol(WHT); APE("Unable to fetch some archives, maybe run apt update",0); return done; }
        if(r->size&&n!=r->size){ kfree(deb); APE("Size mismatch for ",url); return done; }
        if(r->sha.n==64){ u8 h[32]; extern volatile int g_frz_hold; int ofh=g_frz_hold; g_frz_hold=1; sha256_hash(deb,(int)n,h); g_frz_hold=ofh; int ok=1; for(int k=0;k<32;k++){ int v=ap_hexval(r->sha.p[k*2])*16+ap_hexval(r->sha.p[k*2+1]); if(v!=h[k]){ ok=0; break; } } if(!ok){ kfree(deb); APE("Hash Sum mismatch for ",url); return done; } }
        char nm[64]; svcpy(nm,r->name,64);
        int ok=deb_install(deb,n,nm,g_planauto[i],0); kfree(deb); if(!ok)return done; done++; }
    return done; }
typedef struct { char path[200]; char name[64]; char ver[64]; char dep[1024]; } ldeb;
static ldeb g_ldeb[8]; static int g_nldeb;
static int is_local_deb(const char* nm){ int L=asl(nm); return L>4&&nm[L-4]=='.'&&nm[L-3]=='d'&&nm[L-2]=='e'&&nm[L-1]=='b'; }
static u8* local_read(const char* fn,long* n,int* owned){
    long L=0; const u8* d=rfile(fn,&L); if(d){ u8* c=(u8*)kmalloc((unsigned long)L+64); if(!c)return 0; for(long i=0;i<L;i++)c[i]=d[i]; for(int i=0;i<64;i++)c[L+i]=0; *n=L; *owned=1; return c; }
    static const long caps[]={256L<<20,128L<<20,64L<<20,0};
    for(int i=0;caps[i];i++){ u8* b=(u8*)kmalloc((unsigned long)caps[i]); if(!b)continue; int got=fat_read_file(fn,b,(int)caps[i]); if(got<=0){ kfree(b); return 0; } *n=got; *owned=1; return b; }
    return 0; }
static int local_ctl(const char* fn,ldeb* L){
    long n=0; int own=0; u8* deb=local_read(fn,&n,&own); if(!deb){ APE("cannot access archive: ",fn); return 0; }
    const u8* cm; long cl; int ok=0;
    if(ar_member(deb,n,"control.tar",&cm,&cl)){ long cn=0; u8* ctl=unp_auto(cm,cl,&cn);
        if(ctl){ const u8* cf; long cfl; if(tar_scan(ctl,cn,0,"./control",&cf,&cfl)==1){ char pd[512], dd[1024]; ctl_field(cf,cfl,"Package",L->name,64); ctl_field(cf,cfl,"Version",L->ver,64); ctl_field(cf,cfl,"Pre-Depends",pd,512); ctl_field(cf,cfl,"Depends",dd,1024);
                char* p=L->dep; *p=0; if(pd[0]){ p=sj(p,pd); if(dd[0])p=sj(p,", "); } if(asl(L->dep)+asl(dd)<1023)sj(p,dd); ok=L->name[0]!=0; }
            kfree(ctl); } }
    if(own)kfree(deb);
    if(!ok)APE("not a Debian package archive: ",fn);
    int k=0; for(;fn[k]&&k<199;k++)L->path[k]=fn[k]; L->path[k]=0;
    return ok; }
static int local_install(ldeb* L){
    long n=0; int own=0; u8* d=local_read(L->path,&n,&own); if(!d){ APE("cannot access archive: ",L->path); return 0; }
    int ok=deb_install(d,n,0,0,0); if(own)kfree(d); return ok; }
static void apt_install(const char* args,int reinstall){
    if(!idx_load(0))return; st_load(); g_nplan=0; g_nldeb=0; int nreq=0;
    AP("Reading package lists... Done\nBuilding dependency tree... Done\nReading state information... Done\n"); sh_flush();
    const char* a=args;
    for(;;){ while(*a==' ')a++; if(!*a)break; char nm[200]; int k=0; while(*a&&*a!=' '&&k<199)nm[k++]=*a++; nm[k]=0; if(nm[0]=='-')continue;
        if(is_local_deb(nm)){ if(g_nldeb>=8){ APE("too many package files, the limit is 8",0); return; } ldeb* L=&g_ldeb[g_nldeb]; if(!local_ctl(nm,L))return; g_nldeb++;
            tcol(GREY); AP("Note, selecting '"); AP(L->name); AP("' instead of '"); AP(nm); APL("'"); tcol(WHT);
            if(L->dep[0]&&!resolve_list(svs(L->dep),1)){ APE("Unmet dependencies. Cannot satisfy: ",g_unmet); return; }
            nreq++; continue; }
        { static const char* const al[]={"ide","python-ide","pyide",0}; for(int q=0;al[q];q++) if(seq(nm,al[q])){ char* p=nm; p=sj(p,"python3-ide"); break; } }
        sv n=svs(nm); for(int q=0;q<n.n;q++) if(nm[q]=='='){ n.n=q; break; }
        if(is_prot(n)){ sv pv; int r0=idx_best(n); if(!(prov_ver(n,&pv)&&r0>=0&&vcmp(g_pr[r0].ver,pv)>0)){ AP(nm); APL(" is already the newest version (provided by NoovexOS)."); continue; } }
        int si=st_find(n); int v=0; int r=idx_find(n,&v);
        if(r<0){ APE("Unable to locate package ",nm);
            if(!g_have_remote){ tcol(rgb(230,200,120)); APL("N: Only the built-in package list is loaded. To install software from Debian,"); APL("N: download the full package list first:  sudo apt update"); tcol(WHT); }
            if(seq(nm,"claude-desktop")){ tcol(rgb(230,200,120)); APL("N: claude-desktop comes from Anthropic's own repository. Add it with:"); APL("N:   echo \"deb https://downloads.claude.ai/claude-desktop/apt/stable stable main\" > /etc/apt/sources.list.d/claude-desktop.list"); APL("N: then run  sudo apt update  and try again."); tcol(WHT); }
            return; }
        if(si>=0&&!reinstall&&vcmp(g_pr[r].ver,svs(g_in[si].ver))<=0){ g_in[si].autoi=0; st_save(); AP(g_in[si].name); AP(" is already the newest version ("); AP(g_in[si].ver); APL(")."); continue; }
        if(v){ tcol(GREY); AP("Note, selecting '"); APSV(g_pr[r].name); AP("' instead of '"); AP(nm); APL("'"); tcol(WHT); }
        if(!resolve(r,0,0)){ APE("Unmet dependencies. Cannot satisfy: ",g_unmet); return; }
        for(int i=0;i<g_nplan;i++) if(g_plan[i]==r)g_planauto[i]=0;
        nreq++; }
    { int before=g_nplan; for(int i=0;i<g_nin;i++){ int r=idx_best(svs(g_in[i].name)); if(r<0)continue; if(g_pr[r].pre.n)resolve_list(g_pr[r].pre,1); if(g_pr[r].dep.n)resolve_list(g_pr[r].dep,1); }
      if(g_nplan>before){ tcol(rgb(230,200,120)); APN(g_nplan-before); APL(" packages that installed programs need are missing or too old; they are added."); tcol(WHT); } }
    if(!g_nplan&&!g_nldeb){ ld_cache_build(); APL("0 upgraded, 0 newly installed, 0 to remove and 0 not upgraded."); return; }
    if(g_nplan)plan_print(nreq-g_nldeb);
    if(g_nldeb){ APL("Package files to install:"); for(int i=0;i<g_nldeb;i++){ AP("  "); AP(g_ldeb[i].name); AP(" ("); AP(g_ldeb[i].ver); AP(") from "); APL(g_ldeb[i].path); } }
    if(!g_yes&&g_nplan>nreq-g_nldeb){ AP("Do you want to continue? [Y/n] "); sh_flush(); if(!sh_yesno(1)){ APL("Abort."); return; } }
    int d=g_nplan?plan_run():0; if(d!=g_nplan){ ld_cache_build(); return; }
    int dl=0; if(g_nldeb){ extern volatile int g_frz_hold; int ofh=g_frz_hold; g_frz_hold=1; for(int i=0;i<g_nldeb;i++){ if(!local_install(&g_ldeb[i]))break; dl++; } g_frz_hold=ofh; }
    ld_cache_build(); if(dl==g_nldeb){ tcol(GREEN); AP("Installed "); APN(d+dl); APL(d+dl==1?" package.":" packages."); tcol(WHT); } }
static int remove_one(const char* nm,int purge){
    int si=st_find(svs(nm)); if(si<0){ AP("Package '"); AP(nm); APL("' is not installed, so not removed"); return 0; }
    AP("Removing "); AP(g_in[si].name); AP(" ("); AP(g_in[si].ver); APL(") ...");
    char tag[72]; char* p=tag; p=sj(p,"pkg:"); p=sj(p,g_in[si].name); sysroot_del_layer(tag);
    char lf[128]; p=lf; p=sj(p,AP_PKGDIR "/"); p=sj(p,g_in[si].name); p=sj(p,".list"); long mn=0; const u8* md=rfile(lf,&mn);
    if(md){ u8* mc=(u8*)kmalloc((unsigned long)(mn?mn:1)); if(mc){ for(long i=0;i<mn;i++)mc[i]=md[i]; long i=0; while(i<mn){ long e=i; while(e<mn&&mc[e]!='\n')e++; char pb[256]; long c=e-i<255?e-i:255; for(long k=0;k<c;k++)pb[k]=(char)mc[i+k]; pb[c]=0; if(c>0)k_unlink(pb); i=e+1; } kfree(mc); } k_unlink(lf); }
    char fn[128]; p=fn; p=sj(p,AP_PKGDIR "/"); p=sj(p,g_in[si].name); p=sj(p,".nvs"); k_unlink(fn);
    for(int i=si;i<g_nin-1;i++)g_in[i]=g_in[i+1]; g_nin--; st_save(); { extern int g_apps_gen; g_apps_gen++; } (void)purge; return 1; }
static int needed_by_others(int idx){
    sv me=svs(g_in[idx].name);
    for(int i=0;i<g_nin;i++){ if(i==idx)continue; const char* s=g_in[i].dep; const char* e=s+asl(s);
        while(s<e){ while(s<e&&(*s==' '||*s==','||*s=='|'))s++; const char* a=s; while(s<e&&*s!=' '&&*s!=','&&*s!='|'&&*s!='('&&*s!=':')s++; sv t; t.p=a; t.n=(int)(s-a); if(t.n&&sveqv(t,me))return 1; while(s<e&&*s!=','&&*s!='|')s++; } }
    return 0; }
static void apt_autoremove(void){
    st_load(); int n=0;
    for(int pass=0;pass<8;pass++){ int any=0; for(int i=0;i<g_nin;i++){ if(!g_in[i].autoi||needed_by_others(i))continue; char nm[64]; for(int k=0;k<64;k++)nm[k]=g_in[i].name[k]; if(remove_one(nm,0)){ n++; any=1; i--; } } if(!any)break; }
    APN(n); APL(" packages removed."); }
static void apt_remove(const char* args,int purge){
    st_load(); const char* a=args; int n=0;
    for(;;){ while(*a==' ')a++; if(!*a)break; char nm[96]; int k=0; while(*a&&*a!=' '&&k<95)nm[k++]=*a++; nm[k]=0; if(nm[0]=='-')continue;
        if(is_prot(svs(nm))){ APE(nm," is part of NoovexOS and cannot be removed"); continue; }
        n+=remove_one(nm,purge); }
    APN(n); APL(n==1?" package removed.":" packages removed."); ld_cache_build();
    int orphan=0; for(int i=0;i<g_nin;i++) if(g_in[i].autoi&&!needed_by_others(i))orphan++;
    if(orphan){ APN(orphan); APL(" automatically installed packages are no longer required. Use 'sudo apt autoremove' to remove them."); } }
static void apt_list(const char* args){
    int inst=0, upg=0; const char* a=args; char pat[64]; pat[0]=0;
    for(;;){ while(*a==' ')a++; if(!*a)break; char t[64]; int k=0; while(*a&&*a!=' '&&k<63)t[k++]=*a++; t[k]=0; if(seq(t,"--installed"))inst=1; else if(seq(t,"--upgradable"))upg=1; else { for(int q=0;q<64;q++){ pat[q]=t[q]; if(!t[q])break; } } }
    st_load();
    if(inst){ APL("Listing... Done"); for(int i=0;i<g_nin;i++){ tcol(GREEN); AP(g_in[i].name); tcol(WHT); AP("/now "); AP(g_in[i].ver); AP(" "); AP(g_in[i].arch[0]?g_in[i].arch:"amd64"); APL(g_in[i].autoi?" [installed,automatic]":" [installed]"); } return; }
    if(!idx_load(0))return; APL("Listing... Done"); int shown=0;
    for(int i=0;i<g_npr&&shown<400;i++){ prec* r=&g_pr[i]; int b=idx_best(r->name); if(b!=i)continue;
        int si=st_find(r->name); if(upg&&(si<0||vcmp(r->ver,svs(g_in[si].ver))<=0))continue;
        if(pat[0]){ int pl=asl(pat); int m=0; for(int s=0;s+pl<=r->name.n&&!m;s++){ int ok=1; for(int k=0;k<pl;k++){ if(pat[k]=='*')continue; if(r->name.p[s+k]!=pat[k]){ ok=0; break; } } if(ok)m=1; } if(!m)continue; }
        tcol(GREEN); APSV(r->name); tcol(WHT); AP("/stable "); APSV(r->ver); AP(" "); APSV(r->arch); if(si>=0)AP(" [installed]"); term_putc('\n'); shown++; if((shown&15)==0)sh_flush(); }
    if(shown>=400)APL("... (list truncated - use a pattern: apt list 'lib*')"); sh_flush(); }
static int ci_has(sv h,const char* n){ int nl=asl(n); for(int s=0;s+nl<=h.n;s++){ int ok=1; for(int k=0;k<nl;k++){ char a=h.p[s+k], b=n[k]; if(a>='A'&&a<='Z')a+=32; if(b>='A'&&b<='Z')b+=32; if(a!=b){ ok=0; break; } } if(ok)return 1; } return 0; }
static void apt_search(const char* args){
    while(*args==' ')args++; if(!*args){ APL("usage: apt search <word>"); return; } if(!idx_load(0))return; st_load();
    APL("Sorting... Done\nFull Text Search... Done"); int shown=0;
    for(int i=0;i<g_npr&&shown<120;i++){ prec* r=&g_pr[i]; if(idx_best(r->name)!=i)continue; if(!ci_has(r->name,args)&&!ci_has(r->desc,args))continue;
        tcol(GREEN); APSV(r->name); tcol(WHT); AP("/stable "); APSV(r->ver); AP(" "); APSV(r->arch); if(st_find(r->name)>=0)AP(" [installed]"); term_putc('\n'); tcol(GREY); AP("  "); APSV(r->desc); term_putc('\n'); tcol(WHT); shown++; if((shown&7)==0)sh_flush(); }
    if(!shown)APL("(no matches)"); else if(shown>=120)APL("... (more results - try a longer word)"); sh_flush(); }
static void apt_show(const char* args){
    while(*args==' ')args++; char nm[96]; int k=0; while(args[k]&&args[k]!=' '&&k<95){ nm[k]=args[k]; k++; } nm[k]=0; if(!nm[0]){ APL("usage: apt show <package>"); return; }
    if(!idx_load(0))return; int r=idx_find(svs(nm),0); if(r<0){ APE("No packages found: ",nm); return; }
    sv s=g_pr[r].stanza; char line[300]; const char* p=s.p; const char* e=p+s.n; int nl=0;
    while(p<e&&nl<60){ const char* l=p; while(p<e&&*p!='\n')p++; int n=(int)(p-l); if(p<e)p++; int c=n<299?n:299; for(int i=0;i<c;i++)line[i]=l[i]; line[c]=0;
        if(line[0]=='M'&&line[1]=='D'&&line[2]=='5')continue; if(line[0]=='S'&&line[1]=='H'&&line[2]=='A')continue;
        APL(line); nl++; }
    st_load(); AP("APT-Manual-Installed: "); APL(st_find(g_pr[r].name)>=0?"yes":"no"); }
static void apt_upgrade(void){
    if(!idx_load(0))return; st_load(); g_nplan=0; int nreq=0;
    for(int i=0;i<g_nin;i++){ int r=idx_best(svs(g_in[i].name)); if(r<0||vcmp(g_pr[r].ver,svs(g_in[i].ver))<=0)continue; if(!resolve(r,g_in[i].autoi,0)){ APE("Unmet dependencies: ",g_unmet); return; } nreq++; }
    if(!g_nplan){ APL("0 upgraded, 0 newly installed, 0 to remove and 0 not upgraded."); return; }
    plan_print(g_nplan); if(!g_yes){ AP("Do you want to continue? [Y/n] "); sh_flush(); if(!sh_yesno(1)){ APL("Abort."); return; } }
    plan_run(); (void)nreq; }

static void apt_help(void){
    APL("apt 2.9 (NoovexOS) - install Debian packages (amd64)");
    APL("Usage: apt [options] command");
    APL("  update             download the package lists from " AP_SOURCES);
    APL("  install <pkg...>   install packages and their dependencies (-y: don't ask)");
    APL("  reinstall <pkg>    download and unpack again");
    APL("  remove <pkg...>    remove packages (purge = remove)");
    APL("  autoremove         remove packages that were only needed as dependencies");
    APL("  upgrade            upgrade installed packages");
    APL("  search <word>      search names and descriptions");
    APL("  show <pkg>         show package details");
    APL("  list [--installed|--upgradable] [pattern]");
    APL("Packages unpack into the system image layer (programs land in /usr/bin and");
    APL("run through the glibc layer). Maintainer scripts are not run.");
    sh_flush(); }
static int need_root2(void){ if(g_uid==0)return 1; tcol(rgb(240,120,120)); APL("E: Could not open lock file /var/lib/dpkg/lock-frontend - open (13: Permission denied)"); APL("E: Unable to acquire the dpkg frontend lock, are you root? (try: sudo apt ...)"); tcol(WHT); return 0; }

void apt_cmd(const char* arg){
    const char* a=arg; g_yes=0; char sub[24]; sub[0]=0;
    for(;;){ while(*a==' ')a++; if(*a=='-'){ if((a[1]=='y'&&(a[2]==' '||!a[2]))||(a[1]=='-'&&a[2]=='y'))g_yes=1; while(*a&&*a!=' ')a++; continue; } int k=0; while(*a&&*a!=' '&&k<23)sub[k++]=*a++; sub[k]=0; break; }
    { const char* q=a; while(*q){ if(q[0]==' '&&q[1]=='-'&&q[2]=='y'&&(q[3]==' '||!q[3]))g_yes=1; q++; } }
    g_postn=0; g_post[0]=0; g_runpn=0; g_runp[0]=0;
    if(seq(sub,"update")){ if(need_root2())apt_update(); }
    else if(seq(sub,"install")){ if(need_root2())apt_install(a,0); }
    else if(seq(sub,"reinstall")){ if(need_root2())apt_install(a,1); }
    else if(seq(sub,"remove")||seq(sub,"purge")){ if(need_root2())apt_remove(a,seq(sub,"purge")); }
    else if(seq(sub,"autoremove")){ if(need_root2())apt_autoremove(); }
    else if(seq(sub,"upgrade")||seq(sub,"full-upgrade")||seq(sub,"dist-upgrade")){ if(need_root2())apt_upgrade(); }
    else if(seq(sub,"search")){ apt_search(a); }
    else if(seq(sub,"show")||seq(sub,"policy")){ apt_show(a); }
    else if(seq(sub,"list")){ apt_list(a); }
    else if(seq(sub,"clean")||seq(sub,"autoclean")){ }
    else apt_help();
    post_run(); }

static void dpkg_cmd2(const char* arg);
void dpkg_cmd(const char* arg){ g_postn=0; g_post[0]=0; g_runpn=0; g_runp[0]=0; dpkg_cmd2(arg); post_run(); }
static void dpkg_cmd2(const char* arg){
    const char* a=arg; while(*a==' ')a++; char op[24]; int k=0; while(*a&&*a!=' '&&k<23)op[k++]=*a++; op[k]=0; while(*a==' ')a++;
    if(seq(op,"-i")||seq(op,"--install")){ if(!need_root2())return;
        char fn[160]; int f=0; while(a[f]&&a[f]!=' '&&f<159){ fn[f]=a[f]; f++; } fn[f]=0; if(!fn[0]){ APL("dpkg: error: --install needs at least one package archive file argument"); return; }
        long n=0; const u8* d=rfile(fn,&n);
        if(!d){ static u8 g_dbuf[1]; (void)g_dbuf; long cap=96L<<20; u8* b=(u8*)kmalloc((unsigned long)cap); if(b){ int got=fat_read_file(fn,b,(int)cap); if(got>0){ n=got; st_load(); AP("Selecting previously unselected package.\n"); deb_install(b,n,0,0,1); ld_cache_build(); } else APE("cannot access archive: ",fn); kfree(b); } return; }
        u8* c=(u8*)kmalloc((unsigned long)n); if(!c)return; for(long i=0;i<n;i++)c[i]=d[i]; st_load(); deb_install(c,n,0,0,1); kfree(c); ld_cache_build(); }
    else if(seq(op,"-r")||seq(op,"-P")||seq(op,"--remove")||seq(op,"--purge")){ if(!need_root2())return; apt_remove(a,0); }
    else if(seq(op,"-l")||seq(op,"--list")){ st_load();
        APL("Desired=Unknown/Install/Remove/Purge/Hold"); APL("| Status=Not/Inst/Conf-files/Unpacked/halF-conf/Half-inst/trig-aWait/Trig-pend"); APL("||/ Name                    Version              Architecture Description"); APL("+++-=======================-====================-============-================================");
        for(int i=0;i<g_nin;i++){ AP("ii  "); AP(g_in[i].name); for(int s=asl(g_in[i].name);s<24;s++)term_putc(' '); AP(g_in[i].ver); for(int s=asl(g_in[i].ver);s<21;s++)term_putc(' '); { const char* ar=g_in[i].arch[0]?g_in[i].arch:"amd64"; AP(ar); for(int s2=asl(ar);s2<13;s2++)term_putc(' '); } APL(g_in[i].desc); } }
    else if(seq(op,"-L")||seq(op,"--listfiles")){ char pkg[96]; { int q=0; while(a[q]&&a[q]!=' '&&q<95){ pkg[q]=a[q]; q++; } pkg[q]=0; }
        char lf[128]; char* p=sj(sj(sj(lf,AP_PKGDIR "/"),pkg),".list"); (void)p; long mn=0; const u8* md=rfile(lf,&mn);
        if(md){ long i=0; while(i<mn){ long e=i; while(e<mn&&md[e]!='\n')e++; char pb[256]; long c=e-i<255?e-i:255; for(long k=0;k<c;k++)pb[k]=(char)md[i+k]; pb[c]=0; if(c>0)APL(pb); i=e+1; } return; }
        char tag[96]; p=sj(tag,"pkg:"); for(int q=0;pkg[q]&&q<91;q++)*p++=pkg[q]; *p=0;
        if(!sysroot_has_layer(tag)){ AP("dpkg-query: package '"); AP(pkg); APL("' is not installed"); return; }
        char nm[256]; for(int i=0;sysroot_layer_files(tag,i,nm,256);i++){ APL(nm); } }
    else if(seq(op,"-s")||seq(op,"--status")){ st_load(); int si=st_find(svs(a)); if(si<0){ AP("dpkg-query: package '"); AP(a); APL("' is not installed"); return; }
        AP("Package: "); APL(g_in[si].name); APL("Status: install ok installed"); AP("Version: "); APL(g_in[si].ver); AP("Architecture: "); APL(g_in[si].arch); if(g_in[si].dep[0]){ AP("Depends: "); APL(g_in[si].dep); } AP("Description: "); APL(g_in[si].desc); }
    else if(seq(op,"--print-architecture")){ APL("amd64"); }
    else { APL("Usage: dpkg -i <file.deb> | -r <pkg> | -l | -L <pkg> | -s <pkg> | --print-architecture"); } }

void apt_boot(void){
    mkdirp(AP_SOURCES ".d"); mkdirp("/usr/share/keyrings"); mkdirp("/etc/apt/keyrings");
    char nm[64]; int t=0; int n=0;
    for(int i=0;k_readdir_at(AP_PKGDIR,i,nm,&t)==1&&i<4096;i++){
        int L=asl(nm); if(L<5||nm[L-4]!='.'||nm[L-3]!='n'||nm[L-2]!='v'||nm[L-1]!='s')continue;
        char fn[128]; char* p=fn; p=sj(p,AP_PKGDIR "/"); p=sj(p,nm); long ln=0; const u8* d=rfile(fn,&ln); if(!d||ln<16)continue;
        u8* c=(u8*)kmalloc((unsigned long)ln); if(!c)continue; for(long q=0;q<ln;q++)c[q]=d[q];
        char tag[80]; p=tag; p=sj(p,"pkg:"); for(int q=0;q<L-4&&q<70;q++)*p++=nm[q]; *p=0;
        if(sysroot_add_layer(tag,c,(u32)ln,1))n++; else kfree(c); }
    (void)n;
    { long mn=0; if(rfile(AP_STATUS,&mn)&&mn>0)ld_cache_build(); } }
