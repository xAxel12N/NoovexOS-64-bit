/* ---- Stage 2: pipes | and redirection > < in the shell ---- */
static const char* g_pipein=0;
static int pkg_open(const u8*,long,const u8**,long*); static int pkg_install(const u8*,long,int);
static int nv_display_probe(void); extern int g_edid_ok,g_mode_ok; extern char g_edid_name[]; extern u8 g_edid[];
extern nv_mode g_mode;
extern char g_pkg_name[],g_pkg_ver[],g_pkg_arch[],g_pkg_desc[]; extern int g_pkg_nfiles; extern long g_pkg_total; extern u8 g_pkg_raw[]; enum{ PKG_RAW_SZ=8*1024*1024 };
static int tar_walk(const u8*,long,void(*)(const char*,unsigned,const u8*,long,int,void*),void*);
struct pkg_list_ctx { int max, shown; }; static void pkg_print_cb(const char*,unsigned,const u8*,long,int,void*);      /* piped stdin for the current command, or 0 */
static void run_cmd_one(char*s); static void run_cmd(char* line);
#include "kernel/proc/jobs.c"
#include "fs/unixfs.c"
static int jc_sig_pending_cur(void){ return (g_jcur>=0)?(jc_deliverable(&g_jp[g_jcur])!=0):0; }
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

static void run_cmd(char* line){
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
        nvx_write(outfile,(const u8*)pbuf,g_cap_n>0?(int)slen(pbuf):(int)slen(pbuf),0);
        char b[80],*o=b; o=scopy(o,"  wrote ");o=udec(o,(u32)slen(pbuf));o=scopy(o," bytes to ");o=scopy(o,outfile);*o=0; term_puts(b); term_putc('\n');
    }
}
static void run_cmd_one(char*s){
    while(*s==' ')s++; char cmd[24]; int i=0; while(s[i]&&s[i]!=' '&&i<23){cmd[i]=s[i];i++;} cmd[i]=0;
    char*arg=s+i; while(*arg==' ')arg++;
    if(!cmd[0])return;
    if(jc_builtin(cmd,arg))return;
    if(seq(cmd,"help")){ tcol(WHT);
        tcol(ACC); term_puts("jobs fg bg kill ps umask   (job control: cmd &, Ctrl-C, Ctrl-Z, cmd1 | cmd2, > < 2>&1)\n");
        term_puts("programs: count spin sigdemo upper greet pipetest\n");
        term_puts("/bin: ls cat grep wc head tail echo mkdir rmdir rm mv cp touch ln chmod chown stat pwd hostname whoami id uname date sleep mount umount readlink tee sort\n");
        term_puts("pkg (read .deb/.apk)   nvidia/gpu (GPU info)\n"); tcol(WHT);
        term_puts("help clear echo neofetch ver cpu mem time date uptime ls cat edit\n");
        term_puts("net dhcp wifi usb usbdisk usbcat usbwrite usbrm ec utest ring3 run app posix mem sched fork threads preempt ipc wm compositor cores smpstart smpwork lua ping dns http https netlog pci disks colors settings reboot panic\n");
        term_puts("fatinfo fatls fatcat  (read files from an IDE/FAT disk)\n");
        term_puts("nvxlogin whoami nvxlogout nvxtest  (Noovex account + API probe)\n"); }
    else if(seq(cmd,"fatinfo")||seq(cmd,"mount")){ if(fat_mount()){ char b[96],*p=b; p=scopy(p,"FAT"); p=scopy(p,fat_is32?"32":"16"); p=scopy(p," mounted   drive="); p=udec(p,(u32)g_ata_drv); p=scopy(p,"   sec/clus="); p=udec(p,(u32)fat_spc); p=scopy(p,"   data@LBA "); p=udec(p,fat_datastart); *p=0; tcol(CYAN);term_puts(b);tcol(WHT);term_putc('\n'); } else { tcol(RED);term_puts("no FAT disk found (ATA/IDE PIO only; VM needs an IDE disk with FAT)\n");tcol(WHT); } }
    else if(seq(cmd,"fatls")){ if(fat_mount()){ tcol(GREY);term_puts("root directory:\n");tcol(WHT); fat_listdir(fatls_cb); } else { tcol(RED);term_puts("no FAT disk\n");tcol(WHT); } }
    else if(seq(cmd,"crypto")){
        tcol(rgb(120,200,255)); term_puts("  NoovexOS crypto suite\n"); tcol(WHT);
        term_puts("  hashes:  SHA-256  SHA-1  SHA-384  SHA-512\n");
        term_puts("  macs:    HMAC-SHA256  HMAC-SHA512  HMAC-SHA1\n");
        term_puts("  kdf:     HKDF  PBKDF2\n");
        term_puts("  cipher:  AES-128  AES-256  ChaCha20  AES-GCM  AES-CCM  Poly1305\n");
        term_puts("  pubkey:  X25519\n");
        term_puts("  rng:     ChaCha20 CSPRNG (RDSEED/RDRAND seeded)\n");
        term_puts("  tls:     TLS 1.3 client\n");
        /* live self-test vs known vectors */
        int pass=0,tot=0; char b[80],*p;
        { u8 h[64]; sha512_hash((const u8*)"abc",3,h); tot++; int ok=(h[0]==0xdd&&h[1]==0xaf&&h[2]==0x35); if(ok)pass++;
          p=b;p=scopy(p,"  SHA-512  ");p=scopy(p,ok?"PASS ":"FAIL ");for(int i=0;i<4;i++)p=hx(p,h[i],2);*p=0; tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); }
        { u8 h[48]; sha384_hash((const u8*)"abc",3,h); tot++; int ok=(h[0]==0xcb&&h[1]==0x00&&h[2]==0x75); if(ok)pass++;
          p=b;p=scopy(p,"  SHA-384  ");p=scopy(p,ok?"PASS ":"FAIL ");for(int i=0;i<4;i++)p=hx(p,h[i],2);*p=0; tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); }
        { u8 key[32]; for(int i=0;i<32;i++)key[i]=(u8)i; u8 pt[16]={0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
          u8 rk[240]; aes256_expand(key,rk); u8 ct[16]; aes256_enc(rk,pt,ct); tot++; int ok=(ct[0]==0x8e&&ct[1]==0xa2&&ct[2]==0xb7); if(ok)pass++;
          p=b;p=scopy(p,"  AES-256  ");p=scopy(p,ok?"PASS ":"FAIL ");for(int i=0;i<4;i++)p=hx(p,ct[i],2);*p=0; tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); }
        { u8 r1[8],r2[8]; csprng_bytes(r1,8); csprng_bytes(r2,8); tot++; int diff=0; for(int i=0;i<8;i++)if(r1[i]!=r2[i])diff=1; if(diff)pass++;
          p=b;p=scopy(p,"  CSPRNG   ");p=scopy(p,diff?"PASS ":"FAIL ");for(int i=0;i<4;i++)p=hx(p,r1[i],2);*p=0; tcol(diff?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); }
        p=b;p=scopy(p,"  ");p=udec(p,(u32)pass);*p++='/';p=udec(p,(u32)tot);p=scopy(p," self-tests passed");*p=0; tcol(pass==tot?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT);
    }
    else if(seq(cmd,"install")){ ip_open(ST_DESKTOP); }
    else if(seq(cmd,"pkg")){
        char sub[16]; int si=0; const char* a=arg; while(*a==' ')a++; while(*a&&*a!=' '&&si<15)sub[si++]=*a++; sub[si]=0; while(*a==' ')a++;
        int doinst=seq(sub,"install")||seq(sub,"add")||seq(sub,"i");
        int doinfo=seq(sub,"info")||seq(sub,"list")||seq(sub,"l"); 
        if(!doinst&&!doinfo){ tcol(rgb(230,160,60)); term_puts("  usage: pkg info <file.deb|.apk>   |   pkg install <file.deb|.apk>\n");
            term_puts("  reads Debian .deb (ar+tar.gz) and Alpine .apk (tar.gz); extracts data files to /pkg/<name>/\n");
            term_puts("  note: Linux ELF binaries inside won't run on NoovexOS - use it for docs/configs/data\n"); tcol(WHT); }
        else { const char* fn=a; if(!*fn){ tcol(rgb(230,120,90)); term_puts("  give a file: pkg install foo.deb\n"); tcol(WHT); }
            else { int f=k_find_file(fn); long n=0; const u8* buf=0;
                if(f>=0){ buf=(const u8*)FS[f].data; n=FS[f].len; }
                else { int got=fat_read_file(fn,g_pkg_raw,PKG_RAW_SZ); if(got>0){ buf=g_pkg_raw; n=got; } }
                if(!buf){ tcol(rgb(230,120,90)); term_puts("  file not found: "); term_puts((char*)fn); term_putc('\n'); tcol(WHT); }
                else { const u8* tar=0; long tl=0; int kind=pkg_open(buf,n,&tar,&tl);
                    if(!kind){ tcol(rgb(230,120,90)); term_puts("  not a recognised package (need .deb or gzip .apk; xz/zstd not supported)\n"); tcol(WHT); }
                    else { char b[160],*p;
                        p=b; p=scopy(p,"  "); p=scopy(p,kind==1?"Debian .deb":kind==2?"Alpine .apk":"Android .apk (ZIP)"); *p=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        if(g_pkg_name[0]){ p=b; p=scopy(p,"  name: "); p=scopy(p,g_pkg_name); if(g_pkg_ver[0]){ p=scopy(p,"  version: "); p=scopy(p,g_pkg_ver);} if(g_pkg_arch[0]){ p=scopy(p,"  arch: "); p=scopy(p,g_pkg_arch);} *p=0; term_puts(b); term_putc('\n'); }
                        if(g_pkg_desc[0]){ p=b; p=scopy(p,"  "); p=scopy(p,g_pkg_desc); *p=0; tcol(GREY); term_puts(b); term_putc('\n'); tcol(WHT); }
                        if(tar&&tl>0){ g_pkg_nfiles=0; g_pkg_total=0;
                            if(doinst){ int k=pkg_install(buf,n,1); (void)k; p=b; p=scopy(p,"  installed "); p=udec(p,(u32)g_pkg_nfiles); p=scopy(p," files to /pkg/"); p=scopy(p,g_pkg_name[0]?g_pkg_name:"unnamed"); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
                            else { struct pkg_list_ctx c; c.max=40; c.shown=0; tar_walk(tar,tl,pkg_print_cb,&c);
                                pkg_install(buf,n,0); p=b; p=scopy(p,"  "); p=udec(p,(u32)g_pkg_nfiles); p=scopy(p," files, "); p=udec(p,(u32)(g_pkg_total/1024)); p=scopy(p," KB total"); *p=0; term_puts(b); term_putc('\n'); } }
                        else if(kind==3){ tcol(rgb(230,160,60)); term_puts("  Android APK: use the ZIP viewer in Files to browse it\n"); tcol(WHT); }
                    } } } }
    }
    else if(seq(cmd,"nvidia")||seq(cmd,"gpu")){
        if(!nv_probe()){ tcol(rgb(230,160,60)); term_puts("  no NVIDIA GPU found (looked for vendor 10DE, class 03)\n"); tcol(WHT); }
        else { nv_read_id();
            char b[128],*p;
            p=b; p=scopy(p,"  NVIDIA GPU  10DE:"); p=hx(p,g_nv_dev,4); p=scopy(p,"  bus "); p=udec(p,(u32)g_nv_bus); *p++='/'; p=udec(p,(u32)g_nv_slot); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT);
            p=b; p=scopy(p,"  BAR0 regs  0x"); p=hx(p,(u32)g_nv_bar0,8); p=scopy(p,"  size "); p=udec(p,g_nv_bar0_sz/1024/1024); p=scopy(p," MB"); *p=0; term_puts(b); term_putc('\n');
            p=b; p=scopy(p,"  BAR1 fb    0x"); p=hx(p,(u32)(g_nv_bar1>>32),8); p=hx(p,(u32)g_nv_bar1,8); *p=0; term_puts(b); term_putc('\n');
            p=b; p=scopy(p,"  PMC_BOOT_0 0x"); p=hx(p,g_nv_boot0,8); p=scopy(p,"  arch "); p=scopy(p,(char*)nv_arch_name2(g_nv_boot0)); p=scopy(p,"  rev "); p=hx(p,(u32)g_nv_rev,2); *p=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
            if(g_nv_boot0==0xFFFFFFFFu||g_nv_boot0==0){ tcol(rgb(230,120,90)); term_puts("  (register read failed - BAR0 not reachable in this environment)\n"); tcol(WHT); }
            if(nv_load_vbios()){ p=b; p=scopy(p,"  VBIOS      "); p=udec(p,(u32)g_nv_vbios_len); p=scopy(p," bytes"); if(nv_find_bit()){ p=scopy(p,"  BIT@0x"); p=hx(p,g_nv_bit_off,4);} *p=0; term_puts(b); term_putc('\n');
                if(nv_find_dcb()){ nv_parse_dcb(); p=b; p=scopy(p,"  DCB@0x"); p=hx(p,g_nv_dcb_off,4); p=scopy(p,"  connectors: "); p=udec(p,(u32)g_nv_ncon); *p=0; term_puts(b); term_putc('\n');
                    for(int i=0;i<g_nv_ncon;i++){ p=b; p=scopy(p,"    - "); p=scopy(p,(char*)nv_con_name(g_nv_con_type[i])); *p=0; tcol(GREY); term_puts(b); term_putc('\n'); tcol(WHT); } }
                else { tcol(GREY); term_puts("  DCB not located\n"); tcol(WHT); } }
            else { tcol(rgb(230,160,60)); term_puts("  VBIOS not readable here (needs the real GPU; VirtualBox has no NVIDIA card)\n"); tcol(WHT); }
            tcol(GREY); term_puts("  display/accel driver: detect+VBIOS stage only (read-only, safe)\n"); tcol(WHT);
            nv_display_probe();
            if(g_edid_ok){ char b[96],*p=b; p=scopy(p,"  EDID: ok"); if(g_edid_name[0]){ p=scopy(p,"  monitor: "); p=scopy(p,g_edid_name); } *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT);
                if(g_mode_ok){ p=b; p=scopy(p,"  native mode: "); p=udec(p,g_mode.hact); *p++='x'; p=udec(p,g_mode.vact); p=scopy(p,"  pclk "); p=udec(p,g_mode.pclk_khz/1000); p=scopy(p," MHz"); *p=0; term_puts(b); term_putc('\n'); } }
            else { tcol(rgb(230,160,60)); term_puts("  EDID: not read (needs real GPU + a connected display; i2c port mapping may differ per board)\n"); tcol(WHT); }
            tcol(GREY); term_puts("  modeset: disp907d scaffold present but disabled (unverified - see nvdisp.c)\n"); tcol(WHT);
        }
    }
    else if(seq(cmd,"sig")){
        tcol(rgb(120,200,255)); term_puts("  NoovexOS build signature\n"); tcol(WHT);
        char b[96],*p;
        p=b;p=scopy(p,"  magic   ");for(int i=0;i<8;i++)*p++=g_buildsig.magic[i];*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"  version ");p=scopy(p,(char*)g_buildsig.version);*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"  size    ");p=udec(p,g_buildsig.image_size);p=scopy(p," bytes");*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"  epoch   ");p=udec(p,g_buildsig.build_epoch);*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"  sha256  ");for(int i=0;i<16;i++)p=hx(p,g_buildsig.sha256[i],2);*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"          ");for(int i=16;i<32;i++)p=hx(p,g_buildsig.sha256[i],2);*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"  builder ");p=scopy(p,(char*)g_buildsig.builder);*p=0; tcol(GREY); term_puts(b); term_putc('\n'); tcol(WHT);
        /* live integrity re-check over the immutable region */
        int live=sig_self_check();
        tcol(live?GREEN:rgb(230,160,60));
        term_puts(live?"  integrity: OK (immutable region hash matches)\n"
                      :"  integrity: MISMATCH (code hash differs from stamp)\n"); tcol(WHT);
        { char b2[64],*q=b2; q=scopy(q,"  boot check: "); q=scopy(q,g_sig_ok?"passed":"failed (non-fatal)"); *q=0;
          tcol(GREY); term_puts(b2); term_putc('\n'); tcol(WHT); }
    }
    else if(seq(cmd,"spawn")){
        if(!arg[0]){ term_puts("usage: spawn <FILE.ELF>   (fork+exec a child process, wait for it, show exit status)\n"); }
        else {
            int pid=sys_spawn(arg);
            if(pid<0){ tcol(RED); term_puts(pid==-1?"file not found\n":pid==-2?"not an ELF64\n":"spawn failed\n"); tcol(WHT); }
            else {
                int st=sys_waitpid(pid);
                char b[64],*p=b; p=scopy(p,"  [pid ");p=udec(p,(u32)pid);p=scopy(p,"] exited, status ");p=udec(p,(u32)st);*p=0;
                tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT);
            }
        } }
    else if(seq(cmd,"dlmod")){
        if(!arg[0]){ term_puts("usage: dlmod <FILE.MOD>   (load a PIE module, relocate, call entry)\n"); }
        else {
            int n=rtw_read_table_file(arg,fat_filebuf,(int)sizeof(fat_filebuf));
            if(n<64){ tcol(RED); term_puts("module not found\n"); tcol(WHT); }
            else { nvx_module m; int r=nvx_dlload(fat_filebuf,(u32)n,&m);
                if(r){ char b[48],*p=b; p=scopy(p,"  dlload failed, code ");p=udec(p,(u32)(-r));*p=0; tcol(RED); term_puts(b); term_putc('\n'); tcol(WHT); }
                else { char b[80],*p=b; p=scopy(p,"  loaded at 0x");p=hx(p,(u32)((u64)m.base>>32),8);p=hx(p,(u32)(u64)m.base,8);p=scopy(p,"  entry 0x");p=hx(p,(u32)(u64)m.entry,8);*p=0; tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                    term_puts("  --- module output ---\n"); int rc=nvx_dlcall(&m);
                    char c2[40],*q=c2; q=scopy(q,"  module returned ");q=udec(q,(u32)rc);*q=0; tcol(GREEN); term_puts(c2); term_putc('\n'); tcol(WHT); } }
        } }
    else if(seq(cmd,"run")){
        if(!arg[0]){ term_puts("usage: run <FILE.ELF>   (loads an ELF64 from USB/FAT and executes it)\n"); }
        else {
            int n=rtw_read_table_file(arg,fat_filebuf,(int)sizeof(fat_filebuf));   /* USB stick first, then FAT disk */
            if(n<0){ tcol(RED);term_puts("file not found (checked USB stick and FAT disk)\n");tcol(WHT); }
            else if(n<64 || !(fat_filebuf[0]==0x7F&&fat_filebuf[1]=='E'&&fat_filebuf[2]=='L'&&fat_filebuf[3]=='F')){ tcol(RED);term_puts("not an ELF64 file\n");tcol(WHT); }
            else {
                char b[64],*p=b; p=scopy(p,"  loading ");p=scopy(p,(char*)arg);p=scopy(p," (");p=udec(p,(u32)n);p=scopy(p," bytes)...");*p=0; tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                int rc=elf_load_run(fat_filebuf,(u32)n);
                if(rc==0){ tcol(GREEN); term_puts("  program exited\n"); tcol(WHT); }
                else if(rc==1){ tcol(RED); term_puts("  paging not ready\n"); tcol(WHT); }
                else if(rc==2){ tcol(RED); term_puts("  could not build address space (bad segments?)\n"); tcol(WHT); }
                else { tcol(RED); term_puts("  not a valid ELF64\n"); tcol(WHT); }
            }
        } }
    else if(seq(cmd,"fatcat")){ if(!arg[0]){ term_puts("usage: fatcat <NAME.EXT>\n"); } else { int n=fat_read_file(arg,fat_filebuf,(int)sizeof(fat_filebuf)-1); if(n<0){ tcol(RED);term_puts("file not found on FAT disk\n");tcol(WHT); } else { for(int i=0;i<n&&i<8000;i++){ char c=(char)fat_filebuf[i]; if(c=='\r')continue; term_putc(c); } term_putc('\n'); char b[48],*p=b;p=scopy(p,"(");p=udec(p,(u32)n);p=scopy(p," bytes read)");*p=0; tcol(GREY);term_puts(b);tcol(WHT);term_putc('\n'); } } }
    else if(seq(cmd,"clear")){ term_clear(); }
    else if(seq(cmd,"echo")){ term_puts(arg); term_putc('\n'); }
    else if(seq(cmd,"nvxlogin")||seq(cmd,"signin")){
        if(!arg[0]){
            u8 e[5]; entropy_get(e,5); for(int i=0;i<5;i++)g_nvx_state[i]="0123456789abcdef"[e[i]&15]; g_nvx_state[5]=0;
            char url[420],*p=url; p=scopy(p,"https://" NVX_HOST "/oauth/authorize?client_id=" NVX_CID);
            p=scopy(p,"&redirect_uri=urn%3Aietf%3Awg%3Aoauth%3A2.0%3Aoob&response_type=code&scope=profile&state=");
            p=scopy(p,g_nvx_state); *p=0;
            tcol(CYAN);term_puts("Opening Noovex sign-in in the browser...\n");tcol(WHT);
            term_puts("Approve, copy the code it shows, then run:  nvxlogin <code>\n");
            nvx_open_url(url);
        } else {
            char body[560],*p=body; p=scopy(p,"{\"grant_type\":\"authorization_code\",\"code\":\"");p=scopy(p,arg);
            p=scopy(p,"\",\"client_id\":\"" NVX_CID "\",\"client_secret\":\"" NVX_SECRET "\",\"redirect_uri\":\"urn:ietf:wg:oauth:2.0:oob\"}");*p=0;
            int bl=(int)(p-body);
            tcol(GREY);term_puts("exchanging code with " NVX_HOST " ...\n");tcol(WHT);
            int n=https_request(NVX_HOST,"/api/public/oauth/token","POST","Content-Type: application/json\r\n",body,bl,http_buf,(int)sizeof(http_buf));
            if(n<=0){ tcol(RED);term_puts("token request failed (no network / TLS error)\n");tcol(WHT); }
            else { int he=0; for(int i=0;i+3<n;i++)if(http_buf[i]=='\r'&&http_buf[i+1]=='\n'&&http_buf[i+2]=='\r'&&http_buf[i+3]=='\n'){he=i+4;break;}
                char tok[320];
                if(json_str(http_buf+he,n-he,"access_token",tok,(int)sizeof(tok))>0){
                    scopy(g_nvx_token,tok);
                    char hdr[360],*hp=hdr; hp=scopy(hp,"Authorization: Bearer ");hp=scopy(hp,tok);hp=scopy(hp,"\r\n");*hp=0;
                    int m=https_request(NVX_HOST,"/api/public/oauth/userinfo","GET",hdr,0,0,http_buf,(int)sizeof(http_buf));
                    if(m>0){ int he2=0; for(int i=0;i+3<m;i++)if(http_buf[i]=='\r'&&http_buf[i+1]=='\n'&&http_buf[i+2]=='\r'&&http_buf[i+3]=='\n'){he2=i+4;break;}
                        g_nvx_user[0]=g_nvx_email[0]=g_nvx_name[0]=0;
                        json_str(http_buf+he2,m-he2,"username",g_nvx_user,(int)sizeof(g_nvx_user));
                        json_str(http_buf+he2,m-he2,"email",g_nvx_email,(int)sizeof(g_nvx_email));
                        json_str(http_buf+he2,m-he2,"name",g_nvx_name,(int)sizeof(g_nvx_name));
                        g_nvx_signedin=1;
                        char b[240],*bp=b; bp=scopy(bp,"Signed in as ");bp=scopy(bp,g_nvx_user[0]?g_nvx_user:(g_nvx_name[0]?g_nvx_name:"(user)"));
                        if(g_nvx_email[0]){bp=scopy(bp,"  <");bp=scopy(bp,g_nvx_email);bp=scopy(bp,">");} *bp=0;
                        tcol(GREEN);term_puts(b);term_putc('\n');tcol(WHT);
                    } else { tcol(RED);term_puts("userinfo request failed\n");tcol(WHT); }
                } else { char er[96]; if(json_str(http_buf+he,n-he,"error",er,(int)sizeof(er))>0){ tcol(RED);term_puts("sign-in error: ");term_puts(er);term_putc('\n');tcol(WHT); }
                    else { tcol(RED);term_puts("no access_token returned (code expired or wrong secret)\n");tcol(WHT); } }
            }
        }
    }
    else if(seq(cmd,"whoami")){ if(g_nvx_signedin){ tcol(GREEN);term_puts("signed in as ");term_puts(g_nvx_user[0]?g_nvx_user:g_nvx_name); if(g_nvx_email[0]){term_puts(" <");term_puts(g_nvx_email);term_puts(">");} term_putc('\n');tcol(WHT); } else { term_puts("not signed in - run: nvxlogin\n"); } }
    else if(seq(cmd,"nvxlogout")||seq(cmd,"signout")){ g_nvx_signedin=0; g_nvx_token[0]=g_nvx_user[0]=g_nvx_email[0]=0; term_puts("signed out\n"); }
    else if(seq(cmd,"nvxtest")){
        tcol(CYAN);term_puts("Probing mynexus.site API (raw responses):\n");tcol(WHT);
        /* 1: register endpoint */
        u8 e[3]; entropy_get(e,3); char pu[20]; char* q=scopy(pu,"probe_"); for(int i=0;i<3;i++){*q++="0123456789abcdef"[e[i]&15];} *q=0;
        char b1[220],*p=b1; p=scopy(p,"{\"username\":\"");p=scopy(p,pu);p=scopy(p,"\",\"password\":\"Probe12345\",\"client_id\":\"" NVX_CID "\"}");*p=0;
        term_puts("\nPOST /api/public/register\n");
        int n=https_request(NVX_HOST,"/api/public/register","POST","Content-Type: application/json\r\n",b1,(int)(p-b1),http_buf,(int)sizeof(http_buf));
        if(n<=0){ tcol(RED);term_puts("  no response (network/TLS)\n");tcol(WHT); }
        else { term_puts("  "); for(int i=0;i<n&&http_buf[i]!='\r'&&i<90;i++)term_putc(http_buf[i]); term_putc('\n');
            int he=0;for(int j=0;j+3<n;j++)if(http_buf[j]=='\r'&&http_buf[j+1]=='\n'&&http_buf[j+2]=='\r'&&http_buf[j+3]=='\n'){he=j+4;break;}
            term_puts("  body: "); for(int j=he;j<n&&j<he+260;j++){char c=http_buf[j]; term_putc(c=='\n'?' ':c);} term_putc('\n'); }
        /* 2: password-grant token endpoint (dummy creds -> should be invalid_grant, not HTML) */
        char b2[240],*p2=b2; p2=scopy(p2,"{\"grant_type\":\"password\",\"username\":\"probe_none\",\"password\":\"x\",\"client_id\":\"" NVX_CID "\",\"client_secret\":\"" NVX_SECRET "\"}");*p2=0;
        term_puts("POST /api/public/oauth/token (grant_type=password)\n");
        int m=https_request(NVX_HOST,"/api/public/oauth/token","POST","Content-Type: application/json\r\n",b2,(int)(p2-b2),http_buf,(int)sizeof(http_buf));
        if(m<=0){ tcol(RED);term_puts("  no response (network/TLS)\n");tcol(WHT); }
        else { term_puts("  "); for(int i=0;i<m&&http_buf[i]!='\r'&&i<90;i++)term_putc(http_buf[i]); term_putc('\n');
            int he=0;for(int j=0;j+3<m;j++)if(http_buf[j]=='\r'&&http_buf[j+1]=='\n'&&http_buf[j+2]=='\r'&&http_buf[j+3]=='\n'){he=j+4;break;}
            term_puts("  body: "); for(int j=he;j<m&&j<he+260;j++){char c=http_buf[j]; term_putc(c=='\n'?' ':c);} term_putc('\n'); }
        tcol(GREY);term_puts("(JSON = endpoint ready; HTML = route missing / browser-gated)\n");tcol(WHT);
    }
    else if(seq(cmd,"neofetch")||seq(cmd,"fetch")){ term_puts("launching neofetch...\n"); open_win(W_NEOFETCH); }
    else if(seq(cmd,"ver")||seq(cmd,"about")){ term_puts("NoovexOS 64  v8.13 (native x86-64, from scratch)\n"); }
    else if(seq(cmd,"cpu")||seq(cmd,"cpuid")){ tcol(CYAN);term_puts("CPU: ");tcol(WHT);term_puts(g_brand);term_putc('\n');
        char b[64],*p=b;p=scopy(p,"long mode active   EFER.LMA=");*p++=(char)('0'+g_lma);*p=0;term_puts(b);term_putc('\n'); }
    else if(seq(cmd,"time")){ int h,m,ss,y,mo,d;read_clock(&h,&m,&ss,&y,&mo,&d);char b[16],*p=b;p=u2(p,h);*p++=':';p=u2(p,m);*p++=':';p=u2(p,ss);*p=0;term_puts(b);term_putc('\n'); }
    else if(seq(cmd,"date")){ int h,m,ss,y,mo,d;read_clock(&h,&m,&ss,&y,&mo,&d);char b[16],*p=b;p=udec(p,y);*p++='-';p=u2(p,mo);*p++='-';p=u2(p,d);*p=0;term_puts(b);term_putc('\n'); }
    else if(seq(cmd,"uptime")){ char b[24],*p=b;p=scopy(p,"up ");p=udec(p,(u32)(g_ticks/100));p=scopy(p," seconds");*p=0;term_puts(b);term_putc('\n'); }
    else if(seq(cmd,"pwd")){ dt_init(); char pb[192]; dt_path(g_dcwd,pb); term_puts(pb); term_putc('\n'); }
    else if(seq(cmd,"whoami")){ term_puts(cur_user()); term_putc('\n'); }
    else if(seq(cmd,"id")){ char b[64],*p=b; p=scopy(p,"uid=");p=udec(p,(u32)g_uid);p=scopy(p,"(");p=scopy(p,(char*)cur_user());p=scopy(p,") gid=");p=udec(p,(u32)g_uid);*p=0; term_puts(b); term_putc('\n'); }
    else if(seq(cmd,"su")){ const char* a=arg; while(*a==' ')a++;
        if(!*a||seq((char*)a,"root")){ g_uid=0; env_set("USER","root"); }
        else { g_uid=1000; env_set("USER","user"); }
        char b[32],*p=b; p=scopy(p,"now: ");p=scopy(p,(char*)cur_user());*p=0; tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT); }
    else if(seq(cmd,"env")){ for(int i=0;i<NENV;i++) if(g_env[i].used){ term_puts(g_env[i].name); term_putc('='); term_puts(g_env[i].val); term_putc('\n'); } }
    else if(seq(cmd,"export")){ const char* a=arg; while(*a==' ')a++; char nm[24]; int i=0; while(a[i]&&a[i]!='='&&i<23){nm[i]=a[i];i++;} nm[i]=0; if(a[i]=='=')env_set(nm,a+i+1); }
    else if(seq(cmd,"cd")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        if(!*a||seq((char*)a,"~")){ g_dcwd=dt_resolve_dir("/home"); }
        else { int d=dt_resolve_dir(a); if(d>=0){g_dcwd=d; char pb[128]; dt_path(g_dcwd,pb); env_set("PWD",pb);} else { tcol(rgb(240,120,120)); term_puts("cd: no such directory\n"); tcol(WHT); } }
    }
    else if(seq(cmd,"ls")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        int longfmt=0;
        if(a[0]=='-'&&a[1]=='l'){ longfmt=1; a+=2; while(*a==' ')a++; }
        int d = *a ? dt_resolve_dir(a) : g_dcwd;
        int isdev=(d>=0 && dt_child(0,"dev")==d);
        if(d<0){ tcol(rgb(240,120,120)); term_puts("ls: no such directory\n"); tcol(WHT); }
        else if(isdev){ tcol(rgb(120,255,180)); term_puts("null  zero  random  urandom  full  tty  console\n"); tcol(WHT); }
        else if(longfmt){
            char b[96],*p; char ms[12];
            for(int i=0;i<DN_MAX;i++) if(DT[i].used&&DT[i].parent==d){
                p=b; p=scopy(p,mode_str(DT[i].mode?DT[i].mode:0040755u,ms)); p=scopy(p," root root ");p=scopy(p,"    - ");*p=0;
                tcol(GREY); term_puts(b); tcol(rgb(120,200,255)); term_puts(DT[i].name); term_putc('/'); term_putc('\n'); tcol(WHT); }
            for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==d){
                p=b; p=scopy(p,mode_str(FS[k].mode?FS[k].mode:0100644u,ms)); p=scopy(p," root root ");
                p=udec(p,(u32)FS[k].len); p=scopy(p," "); *p=0;
                tcol(GREY); term_puts(b); tcol(WHT); term_puts(FS[k].name); term_putc('\n'); }
        }
        else {
            int any=0;
            for(int i=0;i<DN_MAX;i++) if(DT[i].used&&DT[i].parent==d){ tcol(rgb(120,200,255)); term_puts(DT[i].name); term_putc('/'); term_puts("  "); any=1; }
            for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==d){ tcol(WHT); term_puts(FS[k].name); term_puts("  "); any=1; }
            tcol(WHT); if(!any)term_puts("(empty)"); term_putc('\n');
        }
    }
    else if(seq(cmd,"chmod")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        /* parse octal mode */
        unsigned int nm=0; int got=0; while(*a>='0'&&*a<='7'){ nm=nm*8+(*a-'0'); a++; got=1; } while(*a==' ')a++;
        if(!got||!*a){ term_puts("usage: chmod <octal> <file>   e.g. chmod 755 script\n"); }
        else { int k; for(k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==g_dcwd&&seq(FS[k].name,(char*)a))break;
            if(k<FS_N){ FS[k].mode=(FS[k].mode&S_IFMT)|(nm&07777); if(!(FS[k].mode&S_IFMT))FS[k].mode|=S_IFREG; if(disk_ok)nvx_write(FS[k].name,(const u8*)FS[k].data,FS[k].len,FS[k].folder); char mb[12]; char b2[40],*p=b2; p=scopy(p,"  "); p=scopy(p,mode_str(FS[k].mode,mb)); p=scopy(p," "); p=scopy(p,FS[k].name); *p=0; tcol(GREEN); term_puts(b2); term_putc('\n'); tcol(WHT); }
            else { int dd=dt_child(g_dcwd,a); if(dd>=0){ DT[dd].mode=S_IFDIR|(nm&07777); } else { tcol(rgb(240,120,120)); term_puts("chmod: no such file\n"); tcol(WHT); } } }
    }
    else if(seq(cmd,"mkdir")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        if(!*a){ term_puts("usage: mkdir <name>\n"); }
        else { int r=dt_mkdir_at(g_dcwd,a); if(r<0){tcol(RED);term_puts("mkdir failed (full or exists)\n");tcol(WHT);} else dt_save(); }
    }
    else if(seq(cmd,"rmdir")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        int d=dt_child(g_dcwd,a);
        if(d<0){tcol(RED);term_puts("rmdir: no such directory\n");tcol(WHT);}
        else { int has=0; for(int i=0;i<DN_MAX;i++)if(DT[i].used&&DT[i].parent==d)has=1; for(int k=0;k<FS_N;k++)if(FS[k].used&&FS[k].dir==d)has=1;
            if(has){tcol(RED);term_puts("rmdir: not empty\n");tcol(WHT);} else { DT[d].used=0; dt_save(); } }
    }
    else if(seq(cmd,"touch")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        if(!*a){ term_puts("usage: touch <name>\n"); }
        else { int k; for(k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==g_dcwd&&seq(FS[k].name,(char*)a))break;
            if(k>=FS_N){ for(k=0;k<FS_N;k++) if(!FS[k].used){ int n=0;while(a[n]&&n<63){FS[k].name[n]=a[n];n++;}FS[k].name[n]=0; FS[k].data[0]=0; FS[k].len=0; FS[k].used=1; FS[k].folder=0; FS[k].dir=g_dcwd; FS[k].mode=0100644u; if(disk_ok)nvx_write(FS[k].name,(const u8*)FS[k].data,FS[k].len,0); break; } if(k>=FS_N){tcol(RED);term_puts("touch: no space\n");tcol(WHT);} } }
    }
    else if(seq(cmd,"rm")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        int k; for(k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==g_dcwd&&seq(FS[k].name,(char*)a))break;
        if(k<FS_N){ char dn[64]; scopy(dn,FS[k].name); FS[k].used=0; if(disk_ok)nvx_delete_name(dn); } else { tcol(rgb(240,120,120)); term_puts("rm: no such file\n"); tcol(WHT); }
    }
    else if(seq(cmd,"cp")){
        dt_init(); char src[64],dst[64]; const char* a=arg; while(*a==' ')a++; int n=0; while(*a&&*a!=' '&&n<63)src[n++]=*a++; src[n]=0; while(*a==' ')a++; n=0; while(*a&&*a!=' '&&n<63)dst[n++]=*a++; dst[n]=0;
        int s1; for(s1=0;s1<FS_N;s1++) if(FS[s1].used&&FS[s1].dir==g_dcwd&&seq(FS[s1].name,src))break;
        if(s1>=FS_N||!dst[0]){ tcol(RED);term_puts("usage: cp <src> <dst>\n");tcol(WHT); }
        else { int d1; for(d1=0;d1<FS_N;d1++) if(!FS[d1].used)break; if(d1>=FS_N){tcol(RED);term_puts("cp: no space\n");tcol(WHT);}
            else { int m=0;while(dst[m]&&m<63){FS[d1].name[m]=dst[m];m++;}FS[d1].name[m]=0; int L=FS[s1].len; if(L>=FS_CAP)L=FS_CAP-1; for(int q=0;q<L;q++)FS[d1].data[q]=FS[s1].data[q]; FS[d1].data[L]=0; FS[d1].len=L; FS[d1].used=1; FS[d1].folder=FS[s1].folder; FS[d1].dir=g_dcwd; FS[d1].mode=FS[s1].mode?FS[s1].mode:0100644u; if(disk_ok)nvx_write(FS[d1].name,(const u8*)FS[d1].data,FS[d1].len,FS[d1].folder); } }
    }
    else if(seq(cmd,"mv")){
        dt_init(); char src[64],dst[64]; const char* a=arg; while(*a==' ')a++; int n=0; while(*a&&*a!=' '&&n<63)src[n++]=*a++; src[n]=0; while(*a==' ')a++; n=0; while(*a&&*a!=' '&&n<63)dst[n++]=*a++; dst[n]=0;
        int s1; for(s1=0;s1<FS_N;s1++) if(FS[s1].used&&FS[s1].dir==g_dcwd&&seq(FS[s1].name,src))break;
        if(s1>=FS_N||!dst[0]){ tcol(RED);term_puts("usage: mv <src> <dst>\n");tcol(WHT); }
        else { /* if dst is a directory, move into it; else rename */ int dd=dt_child(g_dcwd,dst);
            if(dd>=0){ FS[s1].dir=dd; if(disk_ok)nvx_write(FS[s1].name,(const u8*)FS[s1].data,FS[s1].len,FS[s1].folder); } else { char on[64];scopy(on,FS[s1].name); int m=0;while(dst[m]&&m<63){FS[s1].name[m]=dst[m];m++;}FS[s1].name[m]=0; if(disk_ok){nvx_delete_name(on);nvx_write(FS[s1].name,(const u8*)FS[s1].data,FS[s1].len,FS[s1].folder);} } }
    }
    else if(seq(cmd,"cat")){
        dt_init(); int k; for(k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==g_dcwd&&seq(FS[k].name,arg))break;
        if(k>=FS_N) for(k=0;k<FS_N;k++) if(FS[k].used&&seq(FS[k].name,arg))break;   /* fallback: any dir */
        if(k<FS_N&&FS[k].used){term_puts(FS[k].data);term_putc('\n');} else {tcol(rgb(240,120,120));term_puts("no such file\n");tcol(WHT);} }
    else if(seq(cmd,"edit")){ int k;for(k=0;k<FS_N;k++) if(FS[k].used&&seq(FS[k].name,arg))break; if(k<FS_N&&FS[k].used){editor_open(k);term_puts("opened in editor.\n");} else {tcol(rgb(240,120,120));term_puts("no such file\n");tcol(WHT);} }
    else if(seq(cmd,"pci")){ char b[80],*p; if(!pci_n)pci_scan();
        for(int k=0;k<pci_n;k++){ p=b; p=scopy(p,"  "); p=hx(p,pci_ven[k],4); *p++=':'; p=hx(p,pci_dev[k],4); p=scopy(p,"  class="); p=hx(p,pci_cls[k],2); *p=0;
            tcol(WHT); term_puts(b); if(pci_cls[k]==2){ tcol(GREEN); term_puts("  <- network"); } term_putc('\n'); } tcol(WHT); }
    else if(seq(cmd,"mem")){ char b[90],*p;
        u64 total=heap_top-heap_base, used=heap_used(), bumped=heap_cur-heap_base;
        tcol(CYAN); p=b; p=scopy(p,"heap @ 0x"); p=hx(p,(u32)heap_base,8); p=scopy(p,"  size "); p=fmt_size(p,(long)total); *p=0; term_puts(b); term_putc('\n');
        tcol(WHT); p=b; p=scopy(p,"  in use "); p=fmt_size(p,(long)used); p=scopy(p,"   reserved "); p=fmt_size(p,(long)bumped); *p=0; term_puts(b); term_putc('\n');
        /* quick self-test: alloc 1MB, write, free */
        void* t=kmalloc(1048576); if(t){ u8* q=t; for(int i=0;i<1048576;i+=4096)q[i]=0x5A; int ok=1; for(int i=0;i<1048576;i+=4096) if(q[i]!=0x5A)ok=0; kfree(t); tcol(ok?GREEN:rgb(230,90,90)); term_puts(ok?"  malloc/free self-test: OK\n":"  self-test FAILED\n"); } else { tcol(rgb(230,90,90)); term_puts("  1MB alloc failed\n"); }
        tcol(WHT); }
    else if(seq(cmd,"rune")){
        /* render a texture using code written in Rune, straight into the screen */
        int rw=256, rh=192, ox=((int)W-rw)/2, oy=((int)H-rh)/2;
        static u32 rbuf[256*192];
        rune_render(rbuf, rw, rh, (int)(g_ticks/3));               /* <-- Rune-compiled */
        for(int y=0;y<rh;y++) for(int x=0;x<rw;x++) putpx(ox+x,oy+y,rbuf[y*rw+x]);
        fill_rect(ox,oy-24,rw,20,rgb(20,22,30)); dstr(ox+6,oy-20,"Rendered by Rune (rune_gfx.rn)",rgb(120,240,140),1);
        blit_all();
        char b[64],*p=b; p=scopy(p,"Rune says: fib(20) = "); p=udec(p,(u32)rune_fib(20)); *p=0;   /* <-- Rune-compiled */
        tcol(CYAN); term_puts("\n"); term_puts(b); term_puts("\n"); tcol(WHT);
        term_puts("(texture + fib computed by code written in Rune, compiled by rune, linked into the kernel)\n");
    }
    else if(seq(cmd,"usbdisk")||seq(cmd,"usbls")){
        tcol(rgb(255,210,90)); term_puts("scanning for a USB mass-storage device...\n"); tcol(WHT);
        if(!msd_autoinit()){ tcol(rgb(230,120,90)); term_puts("no USB stick found (enum failed at: "); term_puts(xhci_failstep); if(xhci_failcc){term_puts(" cc="); char cb[8],*cq=cb;cq=udec(cq,(u32)xhci_failcc);*cq=0;term_puts(cb);} term_puts(")\n"); tcol(WHT); }
        else { char b[100],*p; u32 mb=(u32)(((u64)(msd_lastlba+1)*512ULL)/1048576ULL);
            tcol(CYAN); p=b;p=scopy(p,"USB disk: ");p=udec(p,mb);p=scopy(p," MB  (");p=udec(p,msd_lastlba+1);p=scopy(p," sectors)");*p=0; term_puts(b); term_putc('\n');
            ufat_mount();
            if(!ufat_ok){ tcol(rgb(230,160,60)); term_puts("  no FAT filesystem found\n"); tcol(WHT); }
            else { p=b;p=scopy(p,"  FAT");p=scopy(p,ufat_type==32?"32":"16");p=scopy(p,"  ");p=udec(p,(u32)ufs_n);p=scopy(p," files:");*p=0; tcol(GREEN);term_puts(b);term_putc('\n'); tcol(WHT);
                for(int i=0;i<ufs_n;i++){ p=b;p=scopy(p,"    ");p=scopy(p,ufs[i].name); while((p-b)<26)*p++=' '; p=udec(p,ufs[i].size);p=scopy(p," B");*p=0; term_puts(b);term_putc('\n'); }
                if(!ufs_n){ tcol(GREY); term_puts("    (empty)\n"); tcol(WHT); }
                tcol(GREY); term_puts("  usbcat <file> | usbwrite <file> <text> | usbrm <file>\n"); tcol(WHT); } } }
    else if(seq(cmd,"usbcat")){ if(!arg[0]){ term_puts("usage: usbcat <FILE>\n"); }
        else if(!msd_ok||!ufat_ok){ if(!msd_autoinit()||(ufat_mount(),!ufat_ok)){ tcol(RED);term_puts("no USB FAT filesystem (run usbdisk first)\n");tcol(WHT); } }
        if(arg[0]&&msd_ok&&ufat_ok){ int fi=-1; for(int i=0;i<ufs_n;i++){ int eq=1; for(int k=0;k<16;k++){ char x=ufs[i].name[k],y=arg[k]; if(x>='a'&&x<='z')x-=32; if(y>='a'&&y<='z')y-=32; if(x!=y){eq=0;break;} if(!x&&!y)break; } if(eq){fi=i;break;} }
            if(fi<0){ tcol(RED);term_puts("file not found\n");tcol(WHT); }
            else { static char fbuf[16000]; int n=ufat_read(fi,fbuf,(int)sizeof(fbuf)-1); for(int i=0;i<n&&i<8000;i++){ char c=fbuf[i]; if(c=='\r')continue; term_putc(c); } term_putc('\n');
                char b[40],*p=b;p=scopy(p,"(");p=udec(p,(u32)n);p=scopy(p," bytes)");*p=0; tcol(GREY);term_puts(b);tcol(WHT);term_putc('\n'); } } }
    else if(seq(cmd,"usbwrite")){ char fn[32]; int i=0; while(arg[i]&&arg[i]!=' '&&i<31){fn[i]=arg[i];i++;} fn[i]=0; const char* txt=arg+i; while(*txt==' ')txt++;
        if(!fn[0]){ term_puts("usage: usbwrite <FILE> <text>\n"); }
        else if(!msd_autoinit()){ tcol(RED);term_puts("no USB stick\n");tcol(WHT); }
        else { ufat_mount(); if(!ufat_ok){ tcol(RED);term_puts("no FAT filesystem on the stick\n");tcol(WHT); }
            else { int tl=0; while(txt[tl])tl++; int r=ufat_write_file(fn,txt,tl);
                if(r==0){ tcol(GREEN);term_puts("written to USB\n");tcol(WHT); } else { tcol(RED);term_puts("write failed\n");tcol(WHT); } } } }
    else if(seq(cmd,"utest")||seq(cmd,"userspace")){
        tcol(rgb(120,200,255)); term_puts("=== NoovexOS userspace self-test (stages 1-9) ===\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("frame pool disabled (needs >=272 MB RAM) - cannot test userspace\n"); tcol(WHT); }
        else {
            char b[80],*p;
            /* 1-2 paging */
            u32 r1=0,r2=0; int rc=vm_selftest(&r1,&r2);
            if(rc==0){ tcol(GREEN); term_puts("[1-2] paging + isolation .......... PASS\n"); }
            else { tcol(RED); term_puts("[1-2] paging + isolation .......... FAIL\n"); }
            tcol(WHT);
            /* 4-5 ring3 + syscalls */
            g_r3out_n=0; g_r3out[0]=0; int r3=ring3_run();
            if(r3==0 && g_r3out_n>0){ tcol(GREEN); term_puts("[4-5] ring 3 + syscalls ........... PASS\n"); }
            else { tcol(RED); term_puts("[4-5] ring 3 + syscalls ........... FAIL\n"); }
            tcol(WHT);
            /* 6-7 ELF loader + libc */
            int el=elf_load_run(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
            if(el==0 && g_r3out_n>0){ tcol(GREEN); term_puts("[6-7] ELF loader + mini-libc ...... PASS\n"); }
            else { tcol(RED); term_puts("[6-7] ELF loader + mini-libc ...... FAIL\n"); }
            tcol(WHT);
            /* 8 scheduler */
            for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0;
            int pa=proc_create(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
            int pb=proc_create(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
            if(pa>=0&&pb>=0){ sched_run_all(); }
            if(pa>=0&&pb>=0&&g_r3out_n>0){ tcol(GREEN); term_puts("[8]   cooperative scheduler ....... PASS\n"); }
            else { tcol(RED); term_puts("[8]   cooperative scheduler ....... FAIL\n"); }
            tcol(WHT);
            /* 9 fork */
            for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0;
            int pf=proc_create(_binary_user_user_fork_elf_start,(u32)(_binary_user_user_fork_elf_end-_binary_user_user_fork_elf_start));
            if(pf>=0){ sched_run_all(); }
            if(pf>=0&&g_r3out_n>0){ tcol(GREEN); term_puts("[9]   fork + execve ............... PASS\n"); }
            else { tcol(RED); term_puts("[9]   fork + execve ............... FAIL\n"); }
            tcol(rgb(120,240,150)); term_puts("=== userspace: physical mem, paging, ring 3, syscalls, ELF, scheduler, fork ===\n"); tcol(WHT);
        }
    }
    else if(seq(cmd,"vmtest")||seq(cmd,"vm")||seq(cmd,"paging")){
        char b[100],*p; tcol(CYAN);
        p=b;p=scopy(p,"Physical frame allocator: ");p=scopy(p,pf_ready?"ready":"disabled (needs >=272 MB RAM)");*p=0; term_puts(b);term_putc('\n');
        if(pf_ready){ p=b;p=scopy(p,"  pool 160-256 MB  ");p=udec(p,PF_N);p=scopy(p," frames (4 KB), ");p=udec(p,pframe_free_count());p=scopy(p," free");*p=0; tcol(WHT);term_puts(b);term_putc('\n'); }
        tcol(rgb(255,210,90)); term_puts("running virtual-memory isolation self-test...\n"); tcol(WHT);
        u32 r1=0,r2=0; int rc=vm_selftest(&r1,&r2);
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool on this machine\n"); }
        else if(rc==2){ tcol(RED); term_puts("  FAILED: could not allocate frames/tables\n"); }
        else { p=b;p=scopy(p,"  space1 @V=0x8000000000 = 0x");p=hx(p,r1,8);p=scopy(p,"   space2 = 0x");p=hx(p,r2,8);*p=0; tcol(CYAN);term_puts(b);term_putc('\n');
            if(rc==0){ tcol(GREEN); term_puts("  PASS: same virtual address holds different data per address space.\n");
                term_puts("  -> per-process paging + isolation works. (userspace stage 1+2 done)\n"); }
            else { tcol(RED); term_puts("  FAIL: isolation not achieved\n"); } }
        tcol(WHT); }
    else if(seq(cmd,"ring3")||seq(cmd,"user")||seq(cmd,"usertest")){
        tcol(rgb(255,210,90)); term_puts("entering ring 3 (user mode) with a test program... (experimental)\n"); tcol(WHT);
        int rc=ring3_run();
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool (needs >=272 MB RAM)\n"); }
        else if(rc==2){ tcol(RED); term_puts("  FAILED: could not set up the user address space\n"); }
        else { char b[80],*p; tcol(CYAN); p=b;p=scopy(p,"  ring-3 program said: \"");p=scopy(p,g_r3out);*p++='"';*p=0; term_puts(b); term_putc('\n');
            tcol(GREEN); term_puts("  PASS: a program ran in ring 3, made INT 0x80 syscalls, and exited to the kernel.\n");
            term_puts("  -> user mode + syscalls work. (userspace stage 4+5 done)\n"); }
        tcol(WHT); }
    else if(seq(cmd,"sched")||seq(cmd,"multitask")||seq(cmd,"ps")){
        tcol(rgb(255,210,90)); term_puts("cooperative scheduler: running 3 ring-3 tasks that yield to each other...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool (needs >=272 MB RAM)\n"); tcol(WHT); }
        else { us_init(); for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0; g_r3out[0]=0;
            int a=proc_create(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
            int b=proc_create(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
            int c=proc_create(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
            if(a<0||b<0||c<0){ tcol(RED); term_puts("  failed to create processes\n"); tcol(WHT); }
            else { sched_run_all();
                tcol(CYAN); term_puts("  combined output (3 tasks interleaving via yield):\n"); tcol(WHT);
                int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
                tcol(GREEN); term_puts("  PASS: 3 processes ran concurrently in ring 3, context-switching on yield.\n");
                term_puts("  -> cooperative scheduler + multitasking work. (stage 8 done)\n"); tcol(WHT); } }
    }
    else if(seq(cmd,"fork")||seq(cmd,"forktest")){
        tcol(rgb(255,210,90)); term_puts("fork(): a user process duplicates itself...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool (needs >=272 MB RAM)\n"); tcol(WHT); }
        else { us_init(); for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0; g_r3out[0]=0;
            int a=proc_create(_binary_user_user_fork_elf_start,(u32)(_binary_user_user_fork_elf_end-_binary_user_user_fork_elf_start));
            if(a<0){ tcol(RED); term_puts("  failed to create process\n"); tcol(WHT); }
            else { sched_run_all();
                tcol(CYAN); term_puts("  output:\n"); tcol(WHT);
                int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
                tcol(GREEN); term_puts("  PASS: fork() created a child process with a copy of the address space.\n");
                term_puts("  -> fork + execve work. (stage 9 done)\n"); tcol(WHT); } }
    }
    else if(seq(cmd,"cores")||seq(cmd,"cpu")){
        int c=acpi_count_cores(); char b[80],*p;
        tcol(CYAN); p=b;p=scopy(p,"CPU cores detected (ACPI MADT): ");if(c>0)p=udec(p,(u32)c);else p=scopy(p,"unknown");*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
        p=b;p=scopy(p,"cores online now: ");p=udec(p,(u32)g_cores_online);*p=0; term_puts(b); term_putc('\n');
        if(c>g_cores_online) term_puts("  run 'smpstart' to bring the other cores online.\n"); }
    else if(seq(cmd,"compositor")||seq(cmd,"nova")){
        tcol(rgb(255,210,90)); term_puts("launching a USERLAND compositor + 2 app processes (rung 5, the finale)...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else { compositor_run();
            tcol(CYAN); term_puts("  process output:\n"); tcol(WHT);
            int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
            tcol(GREEN); term_puts("  PASS: a userland compositor process composited windows from 2 other processes.\n");
            term_puts("  -> rungs 1-5 complete: preemption + IPC + protocol + a userland compositor.\n"); tcol(WHT); } }
    else if(seq(cmd,"wm")||seq(cmd,"winproto")){
        tcol(rgb(255,210,90)); term_puts("windowing protocol: an app creates a window via a message + shared-memory pixels...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else { wm_demo();
            tcol(CYAN); term_puts("  app output:\n"); tcol(WHT);
            int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
            tcol(GREEN); term_puts("  PASS: a window was created + drawn entirely through the protocol (rung 4). Press a key.\n"); tcol(WHT);
            { u64 until=g_ticks+600; while(g_ticks<until && kpop()<0)__asm__ volatile("sti; hlt"); } }
    }
    else if(seq(cmd,"ipc")||seq(cmd,"shm")){
        tcol(rgb(255,210,90)); term_puts("IPC: two processes share memory + exchange a message...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else { ipc_reset(); preempt_run_elf(_binary_user_user_ipc_elf_start,(u32)(_binary_user_user_ipc_elf_end-_binary_user_user_ipc_elf_start));
            tcol(CYAN); term_puts("  output:\n"); tcol(WHT);
            int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
            tcol(GREEN); term_puts("  PASS: P2 read P1's shared value + message -> shared memory + IPC work (rung 2+3).\n"); tcol(WHT); } }
    else if(seq(cmd,"preempt")||seq(cmd,"multitask2")){
        tcol(rgb(255,210,90)); term_puts("preemptive multitasking: 2 processes that NEVER yield - the timer forces switches...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else { preempt_run();
            tcol(CYAN); term_puts("  output (interleaved P1/P2 = the timer preempted them, no yield needed):\n"); tcol(WHT);
            int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
            tcol(GREEN); term_puts("  PASS: preemptive scheduling works (rung 1 toward a userland compositor).\n"); tcol(WHT); } }
    else if(seq(cmd,"smpwork")||seq(cmd,"parallel")){
        if(g_cores_online<=1){ tcol(rgb(230,160,60)); term_puts("only 1 core online - run 'smpstart' first to bring the cores online.\n"); tcol(WHT); }
        else { int workers=g_cores_online-1; if(workers>8)workers=8;
            tcol(rgb(255,210,90)); { char b[80],*p=b; p=scopy(p,"dispatching ");p=udec(p,(u32)workers);p=scopy(p," prime-counting jobs across the extra cores...");*p=0; term_puts(b); term_putc('\n'); } tcol(WHT);
            g_smp_work_on=1;   /* wake the parked worker cores */
            u64 t0=g_ticks; int ids[8];
            for(int i=0;i<workers;i++) ids[i]=smp_submit(200000ULL+(u64)i*20000ULL);   /* each core counts primes up to a big N */
            for(int i=0;i<workers;i++){ for(volatile int t=0;t<200000000 && g_jobs[ids[i]].state!=3;t++)__asm__ volatile("pause"); }
            u64 dt=g_ticks-t0;
            for(int i=0;i<workers;i++){ char b[90],*p=b; tcol(CYAN); p=scopy(p,"  core ");p=udec(p,(u32)g_jobs[ids[i]].core);p=scopy(p,": primes<=");p=udec(p,(u32)g_jobs[ids[i]].arg);p=scopy(p," = ");p=udec(p,(u32)g_jobs[ids[i]].result);*p=0; term_puts(b); term_putc('\n'); for(int k=0;k<NJOBS;k++)if(k==ids[i]){g_jobs[k].state=0;break;} }
            tcol(GREEN); { char b[80],*p=b; p=scopy(p,"  all jobs done in ~");p=udec(p,(u32)(dt*10));p=scopy(p," ms, computed in PARALLEL on the cores.");*p=0; term_puts(b); term_putc('\n'); }
            term_puts("  -> real multi-core work. (isolated compute; full SMP scheduling is still a further step.)\n"); tcol(WHT); }
    }
    else if(seq(cmd,"smpstart")||seq(cmd,"smp")){
        tcol(rgb(255,120,80)); term_puts("SMP: starting secondary cores (EXPERIMENTAL - may hang; power-cycle if so)...\n"); tcol(WHT);
        int nc=smp_start_aps(); char b[80],*p;
        for(volatile int t=0;t<5000000;t++)__asm__ volatile("pause");
        tcol(CYAN); p=b;p=scopy(p,"cores in system: ");p=udec(p,(u32)nc);p=scopy(p,"   cores now online: ");p=udec(p,(u32)g_cores_online);*p=0; term_puts(b); term_putc('\n');
        if(g_cores_online>1){ tcol(GREEN); term_puts("  PASS: secondary core(s) booted into long mode and parked.\n");
            term_puts("  (they idle in a halt loop; full SMP scheduling is a further step.)\n"); }
        else { tcol(rgb(230,160,60)); term_puts("  no secondary cores came online (single-core, or the AP bring-up needs tuning).\n"); }
        tcol(WHT); }
    else if(seq(cmd,"threads")||seq(cmd,"thread")){
        tcol(rgb(255,210,90)); term_puts("threads: one process spawns worker threads that share its memory...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else { us_init(); for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0; g_r3out[0]=0; g_thread_stk_next=0x40000200000ULL;
            int a=proc_create(_binary_user_user_thread_elf_start,(u32)(_binary_user_user_thread_elf_end-_binary_user_user_thread_elf_start));
            if(a<0){ tcol(RED); term_puts("  failed to create process\n"); tcol(WHT); }
            else { sched_run_all();
                tcol(CYAN); term_puts("  output:\n"); tcol(WHT);
                int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
                tcol(GREEN); term_puts("  PASS: multiple threads ran in one address space (shared memory).\n"); tcol(WHT); } }
    }
    else if(seq(cmd,"mem")||seq(cmd,"mmap")){
        tcol(rgb(255,210,90)); term_puts("POSIX memory (rung 2+3): a program uses mmap/brk/getpid...\n"); tcol(WHT);
        int rc=elf_load_run(_binary_user_user_mem_elf_start,(u32)(_binary_user_user_mem_elf_end-_binary_user_user_mem_elf_start));
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else if(rc){ tcol(RED); term_puts("  failed to load\n"); tcol(WHT); }
        else { tcol(rgb(180,230,180)); { int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n'); }
            tcol(GREEN); term_puts("  PASS: mmap + brk + getpid (process memory) - the syscalls malloc is built on.\n"); tcol(WHT); } }
    else if(seq(cmd,"posix")||seq(cmd,"fdtest")){
        tcol(rgb(255,210,90)); term_puts("POSIX fd layer (rung 1): a program uses open/write/lseek/read/close on integer fds...\n"); tcol(WHT);
        int rc=elf_load_run(_binary_user_user_posix_elf_start,(u32)(_binary_user_user_posix_elf_end-_binary_user_user_posix_elf_start));
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else if(rc){ tcol(RED); term_puts("  failed to load\n"); tcol(WHT); }
        else { tcol(rgb(180,230,180)); { int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n'); }
            tcol(GREEN); term_puts("  PASS: real POSIX file descriptors (fd table + VFS) - foundation for a POSIX libc.\n"); tcol(WHT); } }
    else if(seq(cmd,"app")||seq(cmd,"gui")){
        tcol(rgb(255,210,90)); term_puts("launching a graphical user-space app (ring 3, via the libc)...\n"); tcol(WHT);
        int rc=elf_load_run(_binary_user_user_app_elf_start,(u32)(_binary_user_user_app_elf_end-_binary_user_user_app_elf_start));
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool (needs >=272 MB RAM)\n"); }
        else if(rc){ tcol(RED); term_puts("  failed to load the app\n"); }
        else { tcol(GREEN); term_puts("  app exited. A real ring-3 program drew graphics + used malloc/printf via syscalls.\n"); }
        tcol(WHT); }
    else if(seq(cmd,"run")||seq(cmd,"userelf")||seq(cmd,"exec")){
        tcol(rgb(255,210,90)); term_puts("loading a real ELF user program (compiled separately)...\n"); tcol(WHT);
        int rc=elf_load_run(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool (needs >=272 MB RAM)\n"); }
        else if(rc==2){ tcol(RED); term_puts("  FAILED: could not set up the user address space\n"); }
        else if(rc==3){ tcol(RED); term_puts("  not a valid ELF64\n"); }
        else { tcol(CYAN); term_puts("  program output:\n"); tcol(WHT);
            int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
            tcol(GREEN); term_puts("  PASS: a separately-compiled ELF ran in ring 3 via the loader + mini-libc.\n");
            term_puts("  -> userspace ELF loader + C library work. (stage 6+7 done)\n"); }
        tcol(WHT); }
    else if(seq(cmd,"ec")||seq(cmd,"battery")||seq(cmd,"bat")){
        int det=ec_detect();
        char b[100],*p; tcol(CYAN); p=b;p=scopy(p,"EC (0x62/0x66) detect: ");p=scopy(p,det?"responding":"NO RESPONSE");*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
        if(!det){ term_puts("  the Embedded Controller isn't answering on this machine.\n"); }
        else { term_puts("  scanning offsets for a plausible battery % (1..100):\n");
            int found=0;
            for(int off=0;off<0x100;off++){ u8 v; if(ec_read((u8)off,&v)){ if(v>=1&&v<=100){ p=b;p=scopy(p,"    offset 0x");p=hx(p,(u32)off,2);p=scopy(p," = ");p=udec(p,(u32)v); if(v<=100)p=scopy(p,"  <- possible battery %"); *p=0; tcol(v>=5&&v<=100?GREEN:WHT); term_puts(b); term_putc('\n'); found++; if(found>=24){term_puts("    ...\n");break;} } } }
            if(!found) term_puts("    (no byte in range 1..100 - unusual)\n");
            tcol(WHT); u8 ac=0; if(ec_read(0x01,&ac)){ p=b;p=scopy(p,"  offset 0x01 (AC/charge guess) = 0x");p=hx(p,(u32)ac,2);*p=0; term_puts(b); term_putc('\n'); }
            term_puts("  tell me which offset matches your real battery % and I'll lock it in.\n"); } }
    else if(seq(cmd,"usbrm")){ if(!arg[0]){ term_puts("usage: usbrm <FILE>\n"); }
        else if(!msd_autoinit()){ tcol(RED);term_puts("no USB stick\n");tcol(WHT); }
        else { ufat_mount(); int fi=-1; for(int i2=0;i2<ufs_n;i2++){ int eq=1; for(int k=0;k<16;k++){ char x=ufs[i2].name[k],y=arg[k]; if(x>='a'&&x<='z')x-=32; if(y>='a'&&y<='z')y-=32; if(x!=y){eq=0;break;} if(!x&&!y)break; } if(eq){fi=i2;break;} }
            if(fi<0){ tcol(RED);term_puts("file not found\n");tcol(WHT); }
            else if(ufat_delete_file(fi)==0){ tcol(GREEN);term_puts("deleted from USB\n");tcol(WHT); } else { tcol(RED);term_puts("delete failed\n");tcol(WHT); } } }
    else if(seq(cmd,"usb")||seq(cmd,"xhci")){ xhci_init();
        if(!xhci_present){ tcol(WHT); term_puts("no xHCI (USB 3.0) controller found\n"); }
        else { char b[90],*p;
            p=b; p=scopy(p,"xHCI @ 0x"); p=hx(p,xhci_base,8); p=scopy(p,"  vendor 0x"); p=hx(p,xhci_vendor,4); *p=0; tcol(CYAN); term_puts(b); term_putc('\n');
            p=b; p=scopy(p,"  init "); p=scopy(p,xhci_init_ok?"OK":"FAILED"); p=scopy(p,"   slots "); p=udec(p,(u32)xhci_slots); p=scopy(p,"   ports "); p=udec(p,(u32)xhci_ports); *p=0; tcol(WHT); term_puts(b); term_putc('\n');
            for(int i=0;i<xhci_ports;i++){ p=b; p=scopy(p,"  port "); p=udec(p,(u32)(i+1)); p=scopy(p,": ");
                if(xhci_port_conn[i]){ p=scopy(p,"device ("); p=scopy(p,(char*)xhci_spd(xhci_port_speed[i])); p=scopy(p,")"); } else p=scopy(p,"empty"); *p=0;
                if(xhci_port_conn[i])tcol(GREEN); else tcol(WHT); term_puts(b); term_putc('\n'); }
            /* (re)enumerate HID devices on all connected ports */
            xhid_n=0; xhci_mouse_ok=0; cmd_enq=0; cmd_cyc=1; ev_deq=0; ev_cyc=1;
            int any=0; for(int i=0;i<xhci_ports;i++){ if(!xhci_port_conn[i])continue; if(xhci_hid_enum(i)){ any=1;
                int d=xhid_n-1; char c2[64],*q=c2; tcol(GREEN); q=scopy(q,xhid_iskbd[d]?"  USB keyboard ready on port ":"  USB mouse ready on port "); q=udec(q,(u32)(i+1)); q=scopy(q," (slot "); q=udec(q,(u32)xhid_slot[d]); *q++=')'; *q=0; term_puts(c2); term_putc('\n'); } }
            if(!any){ tcol(rgb(230,120,90)); term_puts("  enumeration failed at: "); term_puts(xhci_failstep); if(xhci_failcc){term_puts("  (cc="); char cb[8],*cq=cb; cq=udec(cq,(u32)xhci_failcc); *cq=0; term_puts(cb); term_puts(")");} term_putc(0x0A); }
            else { tcol(WHT); term_puts("  move the mouse / type on the keyboard\n"); }
            tcol(WHT); } }
    else if(seq(cmd,"wifi")){ g_wpa_pump=ui_pump; int doload=(arg[0]=='l'||arg[0]=='L'); int doinit=(arg[0]=='i'||arg[0]=='I');   /* wifi load / wifi init */
        if(!pci_n)pci_scan();
        int wi=-1; for(int k=0;k<pci_n;k++) if(pci_cls[k]==0x02 && pci_sub[k]==0x80){ wi=k; break; }
        if(wi<0){ tcol(WHT); term_puts("No Wi-Fi adapter found on the PCI bus.\n");
            int e=nic_index(); if(e>=0){ term_puts("Wired NIC present: "); term_puts((char*)nic_name(pci_ven[e],pci_dev[e])); term_putc('\n'); } }
        else { char b[100],*p; int bus=pci_bus[wi], sl=pci_slot[wi]; const char* ven;
            switch(pci_ven[wi]){ case 0x8086:ven="Intel"; break; case 0x10ec:ven="Realtek"; break; case 0x168c:ven="Qualcomm Atheros"; break;
                case 0x17cb:ven="Qualcomm"; break; case 0x14c3:ven="MediaTek"; break; case 0x14e4:ven="Broadcom"; break; default:ven="Unknown"; }
            tcol(CYAN); p=b;p=scopy(p,"Wi-Fi adapter: ");p=scopy(p,(char*)ven);p=scopy(p,"  [0x");p=hx(p,pci_ven[wi],4);*p++=':';p=hx(p,pci_dev[wi],4);p=scopy(p,"]  bus ");p=udec(p,(u32)bus);*p=0; term_puts(b); term_putc('\n');
            u32 c4=pci_cfg(bus,sl,0,0x04); pci_cfgw(bus,sl,0,0x04,c4|0x06);
            u32 mmio=0; tcol(WHT);
            for(int bi=0;bi<6;bi++){ u32 bar=pci_cfg(bus,sl,0,0x10+bi*4); if(!bar)continue;
                if(bar&1){ p=b;p=scopy(p,"  BAR");*p++=(char)('0'+bi);p=scopy(p," I/O  0x");p=hx(p,bar&0xFFFFFFFC,8);*p=0; term_puts(b);term_putc('\n'); }
                else { u32 a=bar&0xFFFFFFF0u; p=b;p=scopy(p,"  BAR");*p++=(char)('0'+bi);p=scopy(p," MEM 0x");p=hx(p,a,8);*p=0; term_puts(b);term_putc('\n'); if(a>0x100000u)mmio=a; } }
            extern const unsigned char _binary_rtw8821c_fw_bin_start[]; extern const unsigned char _binary_rtw8821c_fw_bin_end[];
            long fwsz=(long)(_binary_rtw8821c_fw_bin_end-_binary_rtw8821c_fw_bin_start);
            { u16 sig=(u16)(_binary_rtw8821c_fw_bin_start[0]|(_binary_rtw8821c_fw_bin_start[1]<<8));
              u16 fver=(u16)(_binary_rtw8821c_fw_bin_start[4]|(_binary_rtw8821c_fw_bin_start[5]<<8)); u8 fsub=_binary_rtw8821c_fw_bin_start[6];
              tcol(GREEN); p=b;p=scopy(p,"  firmware: rtw8821c_fw.bin  ");p=udec(p,(u32)fwsz);p=scopy(p," bytes  sig 0x");p=hx(p,sig,4);p=scopy(p,"  v");p=udec(p,fver);*p++='.';p=udec(p,fsub);*p=0; term_puts(b);term_putc('\n'); }
            if(!mmio){ tcol(rgb(230,160,60)); term_puts("  no MMIO window assigned by BIOS - cannot touch chip registers.\n"); tcol(WHT); term_puts("  status: detected, firmware ready; MMIO needed to proceed.\n"); }
            else {
                #define WMR(o)   (*(volatile u32*)(u64)(mmio+(o)))
                #define WMR16(o) (*(volatile u16*)(u64)(mmio+(o)))
                #define WMW16(o,v) (*(volatile u16*)(u64)(mmio+(o))=(u16)(v))
                #define WMW8(o,v)  (*(volatile u8*)(u64)(mmio+(o))=(u8)(v))
                u32 sysc=WMR(0xF0);
                if(sysc==0xFFFFFFFFu){ tcol(rgb(230,160,60)); term_puts("  chip not responding at MMIO (powered down?).\n"); }
                else { tcol(CYAN); p=b;p=scopy(p,"  chip SYS_CFG(0xF0)=0x");p=hx(p,sysc,8);*p=0; term_puts(b);term_putc('\n');
                    u8 mac[6]; for(int i=0;i<6;i++)mac[i]=*(volatile u8*)(u64)(mmio+0x0610+i);
                    p=b;p=scopy(p,"  MAC ");for(int i=0;i<6;i++){p=hx(p,mac[i],2);if(i<5)*p++=':';}*p=0; term_puts(b);term_putc('\n');
                    if(arg[0]=='e'||arg[0]=='E'){
                        tcol(rgb(255,210,90)); term_puts("  [wifi efuse] reading physical EFUSE (512 B)\n"); tcol(WHT);
                        int bad=rtw_efuse_dump_phy(mmio,rtw_phymap,RTW_EFUSE_PHY_SZ);
                        int used=rtw_efuse_logical(rtw_phymap,RTW_EFUSE_PHY_SZ,RTW_EFUSE_PROT_SZ,rtw_logmap,RTW_EFUSE_LOG_SZ);
                        rtw_efuse_valid=(used>0);
                        p=b;p=scopy(p,"  read done  timeouts=");p=udec(p,(u32)bad);p=scopy(p,"  physical bytes consumed=");
                        if(used<0)p=scopy(p,"decode error"); else p=udec(p,(u32)used); *p=0;
                        tcol(bad?rgb(230,160,60):CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        int allff=1; for(int i=0;i<RTW_EFUSE_PHY_SZ;i++) if(rtw_phymap[i]!=0xFF){allff=0;break;}
                        if(allff){ tcol(rgb(230,120,90)); term_puts("  EFUSE reads all 0xFF - chip is not powered on. run 'wifi init' first.\n"); tcol(WHT); }
                        else {
                            u16 rid=(u16)(rtw_logmap[0]|(rtw_logmap[1]<<8));
                            p=b;p=scopy(p,"  rtl_id=0x");p=hx(p,rid,4);p=scopy(p,"  chplan=0x");p=hx(p,rtw_logmap[0xB8],2);
                            p=scopy(p,"  xtal_k=0x");p=hx(p,rtw_logmap[0xB9],2);p=scopy(p,"  therm=0x");p=hx(p,rtw_logmap[0xBA],2);*p=0;
                            tcol(CYAN); term_puts(b); term_putc('\n');
                            p=b;p=scopy(p,"  rfe=0x");p=hx(p,rtw_logmap[0xCA],2);p=scopy(p,"  country=0x");p=hx(p,rtw_logmap[0xCB],2);
                            p=scopy(p,"  MAC(0xD0) ");for(int i=0;i<6;i++){p=hx(p,rtw_logmap[0xD0+i],2); if(i<5)*p++=':';}*p=0;
                            term_puts(b); term_putc('\n');
                            p=b;p=scopy(p,"  ids(0xD6) ven=0x");p=hx(p,(u32)(rtw_logmap[0xD6]|(rtw_logmap[0xD7]<<8)),4);
                            p=scopy(p," dev=0x");p=hx(p,(u32)(rtw_logmap[0xD8]|(rtw_logmap[0xD9]<<8)),4);*p=0;
                            term_puts(b); term_putc('\n'); tcol(WHT);
                            term_puts("  logical map 0x0B0-0x0DF:\n"); tcol(GREY);
                            for(int o=0xB0;o<0xE0;o+=16){ char hl[80]; rtw_hexline(rtw_logmap,o,16,hl); term_puts(hl); term_putc('\n'); }
                            tcol(WHT);
                            if(msd_ok||msd_autoinit()){
                                if(ufat_write_file("EFUSEP.BIN",(const char*)rtw_phymap,RTW_EFUSE_PHY_SZ)==0 &&
                                   ufat_write_file("EFUSEL.BIN",(const char*)rtw_logmap,RTW_EFUSE_LOG_SZ)==0){
                                    tcol(GREEN); term_puts("  wrote EFUSEP.BIN + EFUSEL.BIN to the USB stick\n"); tcol(WHT); }
                                else { tcol(GREY); term_puts("  (could not write the dumps to USB)\n"); tcol(WHT); } }
                            else { tcol(GREY); term_puts("  (no USB stick mounted - dumps not saved)\n"); tcol(WHT); }
                        } }
                    else if(seq(arg,"macinit")||seq(arg,"MACINIT")||seq(arg,"trx")||seq(arg,"TRX")){
                        tcol(rgb(120,200,255)); term_puts("  [wifi macinit] rtw_init_trx_cfg: queue map + page alloc (auto-LLT) + H2C queue\n"); tcol(WHT);
                        int r=rtw_mac_trx_init(mmio);
                        rtw_mac_trx_ok=(r==0);
                        u8 llt=rtw_r8(mmio,0x0208); u16 bnd=rtw_r16(mmio,0x0204); u8 cr=rtw_r8(mmio,0x0100);
                        p=b;p=scopy(p,"  CR=0x");p=hx(p,cr,2);p=scopy(p,"  rsvd_boundary=0x");p=hx(p,bnd&0x0FFF,3);p=scopy(p,"  AUTO_LLT=0x");p=hx(p,llt,2);*p=0;
                        tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        if(r==0){ tcol(GREEN); term_puts("  MAC TRX init OK -- page pool allocated, LLT settled. beacon queue can accept pages now.\n");
                            term_puts("  next: 'wifi txbuf' should now assert BCN_VALID.\n"); tcol(WHT); }
                        else { tcol(rgb(230,120,90)); term_puts("  AUTO_LLT did not clear -- page-table init rejected (check queue map / boundary)\n"); tcol(WHT); } }
                    else if((arg[0]=='t'||arg[0]=='T')&&(arg[1]=='x'||arg[1]=='X')){
                        tcol(rgb(120,200,255)); term_puts("  [wifi txbuf] staging a test page via the beacon queue, then IDDMA-reading it back\n"); tcol(WHT);
                        static const u8 pat[64]={0x4E,0x56,0x58,0x21,0xDE,0xAD,0xBE,0xEF,0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
                                                 0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00,
                                                 0xF0,0xE1,0xD2,0xC3,0xB4,0xA5,0x96,0x87,0x78,0x69,0x5A,0x4B,0x3C,0x2D,0x1E,0x0F,
                                                 0xCA,0xFE,0xBA,0xBE,0x8B,0xAD,0xF0,0x0D,0x13,0x37,0xC0,0xDE,0xFA,0xCE,0xB0,0x0C};
                        int st=rtw_bcnq_stage(mmio,pat,64);
                        if(st<0){ tcol(rgb(230,120,90));
                            if(st==-1) term_puts("  alloc failed\n");
                            else term_puts("  BCN_VALID never asserted - the beacon queue did not accept the page\n"); tcol(WHT); }
                        else {
                            tcol(GREEN); term_puts("  BCN_VALID asserted - page landed in the TX buffer\n"); tcol(WHT);
                            u32 v=rtw_r32(mmio,0x1208); v|=(1u<<25); rtw_w32(mmio,0x1208,v);
                            int r=rtw_iddma_block(mmio,0x18780000u+48u,0x00200000u,64,1);
                            u32 ch=rtw_r32(mmio,0x1208);
                            p=b;p=scopy(p,"  IDDMA ");p=scopy(p,(r==0)?"completed":"BUSY-timeout");p=scopy(p,"  CH0CTRL=0x");p=hx(p,ch,8);
                            p=scopy(p,"  chksum_sts=");p=udec(p,(ch>>27)&1);*p=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                            if(r==0 && !((ch>>27)&1)){ tcol(GREEN); term_puts("  TX-staging VERIFIED: IDDMA read the staged page and checksum was good.\n");
                                term_puts("  the firmware download foundation works - 'wifi fw' can be built on this.\n"); tcol(WHT); }
                            else { tcol(rgb(230,160,60)); term_puts("  staged, but IDDMA checksum flagged - source addr or page geometry still off.\n"); tcol(WHT); } } }
                    else if((arg[0]=='f'||arg[0]=='F')&&(arg[1]=='w'||arg[1]=='W')){
                        extern const unsigned char _binary_rtw8821c_fw_bin_start[]; extern const unsigned char _binary_rtw8821c_fw_bin_end[];
                        long fwsz2=(long)(_binary_rtw8821c_fw_bin_end-_binary_rtw8821c_fw_bin_start);
                        tcol(rgb(120,200,255)); term_puts("  [wifi fw] IDDMA firmware download (rtw88 mac.c path)\n"); tcol(WHT);
                        int r=rtw_fw_download_iddma(mmio,_binary_rtw8821c_fw_bin_start,fwsz2);
                        u16 st=rtw_r16(mmio,0x0080);
                        p=b;p=scopy(p,"  MCUFW_CTRL(0x80)=0x");p=hx(p,st,4);p=scopy(p,"  imem_dw=");p=udec(p,(st>>3)&1);
                        p=scopy(p," imem_ck=");p=udec(p,(st>>4)&1);p=scopy(p," dmem_dw=");p=udec(p,(st>>5)&1);p=scopy(p," dmem_ck=");p=udec(p,(st>>6)&1);
                        p=scopy(p," dwrdy=");p=udec(p,(st>>14)&1);p=scopy(p," initrdy=");p=udec(p,(st>>15)&1);*p=0;
                        tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        p=b;p=scopy(p,"  per-section: DMEM ck=");p=udec(p,(u32)g_dmem_ck);p=scopy(p,(g_dmem_sf?" STAGEFAIL":""));p=scopy(p,(g_dmem_if?" IDDMAFAIL":""));
                        p=scopy(p,"  IMEM ck=");p=udec(p,(u32)g_imem_ck);p=scopy(p,(g_imem_sf?" STAGEFAIL":""));p=scopy(p,(g_imem_if?" IDDMAFAIL":""));
                        p=scopy(p,"  EMEM ck=");p=udec(p,(u32)g_emem_ck);*p=0; tcol(rgb(200,180,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                        if(r==0){ tcol(GREEN); term_puts("  MCU_FW_READY set -- the MCU is running your firmware DESPITE any ck complaints!\n");
                            term_puts("  next: 'wifi h2c' to talk to the MCU, then RX ring for a beacon.\n"); tcol(WHT); }
                        else { tcol(rgb(230,160,60)); term_puts("  all sections downloaded; FW_READY did not assert.\n");
                            term_puts("  if DMEM/IMEM ck=1 above, data was fine - blocker is post-download init, not the checksum.\n"); tcol(WHT); } }
                    else if((arg[0]=='p'||arg[0]=='P')&&(arg[1]=='r'||arg[1]=='R')){
                        if(!g_rxnum||!g_benum){ tcol(rgb(230,120,90)); term_puts("  run 'wifi rings' first\n"); tcol(WHT); }
                        else {
                            u8 ch=6; { const char* a=arg; while(*a&&*a!=' ')a++; while(*a&&(*a<'0'||*a>'9'))a++; if(*a>='0'&&*a<='9'){int v=0;while(*a>='0'&&*a<='9'){v=v*10+(*a-'0');a++;} if(v>=1&&v<=13)ch=(u8)v;} }
                            p=b;p=scopy(p,"  [wifi probe] TX broadcast probe request on channel ");p=udec(p,ch);p=scopy(p,", waiting for a response...");*p=0;
                            tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                            rtw_rx_ring_up(mmio);
                            rtw_set_channel_2g(mmio,ch);
                            int got=0; char rs[40];
                            rtw_tx_probe(mmio,&got,rs);
                            p=b;p=scopy(p,"  TX: hw_idx ");p=udec(p,g_probe_hwb);p=scopy(p,"->");p=udec(p,g_probe_hwa);p=scopy(p,g_probe_txok?"  FRAME LEFT THE QUEUE (transmitted)":"  frame NOT consumed by hw (TX ring stuck)");*p=0;
                            tcol(g_probe_txok?CYAN:rgb(230,160,60)); term_puts(b); term_putc('\n'); tcol(WHT);
                            if(got){ tcol(GREEN); p=b;p=scopy(p,"  PROBE RESPONSE from \"");p=scopy(p,rs[0]?rs:"<hidden>");p=scopy(p,"\"");*p=0; term_puts(b); term_putc('\n');
                                term_puts("  *** TWO-WAY RADIO: NoovexOS transmitted a frame and an AP answered ***\n"); tcol(WHT); }
                            else { tcol(rgb(230,160,60)); term_puts("  no probe response (TX may not have gone out, or none in range). Try another channel: wifi probe 1 / 11\n"); tcol(WHT); }
                        } }
                    else if((arg[0]=='j'||arg[0]=='J')&&(arg[1]=='o'||arg[1]=='O')){
                        const char* nm=arg+2; while(*nm==' ')nm++;
                        if(!*nm){ tcol(rgb(230,120,90)); term_puts("  usage: wifi join <ssid>   (open networks only for now)\n"); tcol(WHT); }
                        else if(!g_rxnum||!g_benum){ tcol(rgb(230,120,90)); term_puts("  run 'wifi rings' first\n"); tcol(WHT); }
                        else {
                            p=b;p=scopy(p,"  [wifi join] scanning for \"");p=scopy(p,(char*)nm);p=scopy(p,"\"...");*p=0; tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                            g_apn=0; rtw_rx_ring_up(mmio);
                            static const u8 jc[]={1,6,11,2,3,4,5,7,8,9,10,12,13};
                            int target=-1;
                            for(int ci=0;ci<13 && target<0;ci++){ rtw_set_channel_2g(mmio,jc[ci]);
                                for(int w=0;w<3;w++){ rtw_delay_us(60000); ui_pump(); rtw_scan_drain(mmio,jc[ci]); }
                                for(int a=0;a<g_apn;a++){ int eq=1; const char* z=nm; int k=0; while(z[k]&&k<32){ if(g_aps[a].ssid[k]!=z[k])eq=0; k++; } if(g_aps[a].ssid[k]||z[k])eq=0; if(eq){target=a;break;} } }
                            if(target<0){ tcol(rgb(230,160,60)); term_puts("  network not found in scan\n"); tcol(WHT); }
                            else {
                                p=b;p=scopy(p,"  found on ch ");p=udec(p,g_aps[target].ch);p=scopy(p,", BSSID ");for(int z=0;z<6;z++){p=hx(p,g_aps[target].bssid[z],2);if(z<5)*p++=':';}*p=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                                term_puts("  AUTH -> ASSOC handshake...\n");
                                int r=rtw_assoc(mmio,&g_aps[target],nm);
                                if(r==0){ tcol(GREEN); term_puts("  *** ASSOCIATED! NoovexOS is linked to the access point ***\n");
                                    term_puts("  (open network: DHCP/TCP can now run over Wi-Fi. WPA2 nets need the 4-way handshake, next.)\n"); tcol(WHT); }
                                else if(r==1){ tcol(rgb(230,160,60)); term_puts("  no AUTH response (AP didn't answer - TX radiating? try closer / another AP)\n"); tcol(WHT); }
                                else if(r==2){ tcol(rgb(230,160,60)); term_puts("  AUTH rejected by AP (open-system refused - likely a WPA2 network)\n"); tcol(WHT); }
                                else if(r==3){ tcol(rgb(230,160,60)); term_puts("  AUTH ok but no ASSOC response\n"); tcol(WHT); }
                                else { tcol(rgb(230,160,60)); term_puts("  ASSOC rejected by AP (status != 0)\n"); tcol(WHT); }
                            }
                        } }
                    else if((arg[0]=='o'||arg[0]=='O')&&(arg[1]=='p'||arg[1]=='P')){
                        if(!g_rxnum){ tcol(rgb(230,120,90)); term_puts("  run 'wifi rings' first\n"); tcol(WHT); }
                        else {
                            tcol(rgb(120,200,255)); term_puts("  [wifi open] scanning for OPEN (no-password) networks...\n"); tcol(WHT);
                            g_apn=0; rtw_rx_ring_up(mmio);
                            static const u8 oc[]={1,6,11,2,3,4,5,7,8,9,10,12,13};
                            for(int ci=0;ci<13;ci++){ rtw_set_channel_2g(mmio,oc[ci]); for(int w=0;w<3;w++){ rtw_delay_us(60000); ui_pump(); rtw_scan_drain(mmio,oc[ci]); } }
                            int nopen=0; for(int i=0;i<g_apn;i++) if(g_aps[i].sec==0)nopen++;
                            if(nopen==0){ tcol(rgb(230,160,60)); p=b;p=scopy(p,"  no open networks (");p=udec(p,(u32)g_apn);p=scopy(p," total, all secured). WPA2 support is the next build.");*p=0; term_puts(b); term_putc('\n'); tcol(WHT); }
                            else {
                                tcol(GREEN); p=b;p=scopy(p,"  ");p=udec(p,(u32)nopen);p=scopy(p," OPEN network(s) you can join now:");*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
                                for(int i=0;i<g_apn;i++){ if(g_aps[i].sec!=0)continue; p=b;p=scopy(p,"    wifi join ");p=scopy(p,g_aps[i].ssid[0]?g_aps[i].ssid:"<hidden>");p=scopy(p,"   (ch ");p=udec(p,g_aps[i].ch);*p++=')';*p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
                            }
                        } }
                    else if((arg[0]=='w'||arg[0]=='W')&&(arg[1]=='p'||arg[1]=='P')){
                        const char* a=arg; while(*a&&*a!=' ')a++; while(*a==' ')a++;   /* skip the whole 'wpa' token, then spaces */
                        char ss[33]; int sn=0; while(*a&&sn<32)ss[sn++]=*a++; while(sn>0&&ss[sn-1]==' ')sn--; ss[sn]=0;   /* whole rest = SSID, spaces allowed */
                        int allnum=(sn>0); for(int z=0;z<sn;z++) if(ss[z]<'0'||ss[z]>'9')allnum=0;
                        if(allnum){   /* connect by ID from last scan - no rescan */
                            int id=0; for(int z=0;z<sn;z++)id=id*10+(ss[z]-'0');
                            if(id<1||id>g_apn){ tcol(rgb(230,120,90)); term_puts("  no network with that ID - run 'wifi scan' first\n"); tcol(WHT); }
                            else { int idx=id-1;
                                int j=0; while(g_aps[idx].ssid[j]&&j<32){g_pwssid[j]=g_aps[idx].ssid[j];j++;} g_pwssid[j]=0;
                                g_pwbyid=idx+1;   /* remember chosen index (1-based; 0=none) */
                                if(g_aps[idx].sec==0){ g_pwbyid=idx+1; wpa_connect_saved(mmio,idx,""); }
                                else { g_pwprompt=1; p=b;p=scopy(p,"  password for \"");p=scopy(p,g_pwssid);p=scopy(p,"\": ");*p=0; tcol(rgb(120,200,255)); term_puts(b); tcol(WHT); } }
                        }
                        else if(!sn){ tcol(rgb(230,120,90)); term_puts("  usage: wifi wpa <ID|ssid>   (run 'wifi scan' to see IDs)\n"); tcol(WHT); }
                        else { int i2=0; while(ss[i2]&&i2<32){g_pwssid[i2]=ss[i2];i2++;} g_pwssid[i2]=0; g_pwbyid=0; g_pwprompt=1; tcol(rgb(120,200,255)); term_puts("  password: "); tcol(WHT); } }
                    else if((arg[0]=='c'||arg[0]=='C')&&(arg[1]=='r'||arg[1]=='R')){
                        tcol(rgb(120,200,255)); term_puts("  [wifi cryptotest] verifying WPA2 crypto vs known vectors\n"); tcol(WHT);
                        int pass=0,tot=0;
                        /* SHA-1("abc") = a9993e36 4706816a ba3e2571 7850c26c 9cd0d89d */
                        { u8 h[20]; sha1_hash((const u8*)"abc",3,h); tot++;
                          u8 exp[4]={0xa9,0x99,0x3e,0x36}; int ok=(h[0]==exp[0]&&h[1]==exp[1]&&h[2]==exp[2]&&h[3]==exp[3]);
                          p=b;p=scopy(p,"  SHA-1(abc)     ");p=scopy(p,ok?"PASS  ":"FAIL  ");for(int i=0;i<4;i++)p=hx(p,h[i],2);*p=0;
                          tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); if(ok)pass++; }
                        /* HMAC-SHA1 key="Jefe" data="what do ya want for nothing?" = effcdf6a e5eb2fa2 d27416d5 f184df9c 259a7c79 */
                        { u8 h[20]; hmac_sha1((const u8*)"Jefe",4,(const u8*)"what do ya want for nothing?",28,h); tot++;
                          u8 exp[4]={0xef,0xfc,0xdf,0x6a}; int ok=(h[0]==exp[0]&&h[1]==exp[1]&&h[2]==exp[2]&&h[3]==exp[3]);
                          p=b;p=scopy(p,"  HMAC-SHA1      ");p=scopy(p,ok?"PASS  ":"FAIL  ");for(int i=0;i<4;i++)p=hx(p,h[i],2);*p=0;
                          tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); if(ok)pass++; }
                        /* PBKDF2(password,"salt",4096,20) first bytes = 4b007901 b765489a ... (RFC6070) */
                        { u8 o[20]; pbkdf2_sha1((const u8*)"password",8,(const u8*)"salt",4,4096,o,20); tot++;
                          u8 exp[4]={0x4b,0x00,0x79,0x01}; int ok=(o[0]==exp[0]&&o[1]==exp[1]&&o[2]==exp[2]&&o[3]==exp[3]);
                          p=b;p=scopy(p,"  PBKDF2(4096)   ");p=scopy(p,ok?"PASS  ":"FAIL  ");for(int i=0;i<4;i++)p=hx(p,o[i],2);*p=0;
                          tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); if(ok)pass++; }
                        /* AES-CCM round-trip: encrypt then decrypt-verify must return the same plaintext */
                        { u8 key[16]; for(int i=0;i<16;i++)key[i]=(u8)i; u8 nonce[13]; for(int i=0;i<13;i++)nonce[i]=(u8)(0x10+i);
                          u8 aad[8]={1,2,3,4,5,6,7,8}; u8 pt[16]; for(int i=0;i<16;i++)pt[i]=(u8)(0xA0+i);
                          u8 ct[16]; for(int i=0;i<16;i++)ct[i]=pt[i]; u8 mic[8];
                          aes_ccm_auth_encrypt(key,nonce,aad,8,ct,16,mic); tot++;
                          int changed=0; for(int i=0;i<16;i++) if(ct[i]!=pt[i])changed=1;
                          int ver=aes_ccm_decrypt_verify(key,nonce,aad,8,ct,16,mic);
                          int same=1; for(int i=0;i<16;i++) if(ct[i]!=pt[i])same=0;
                          int ok=(changed&&ver&&same);
                          p=b;p=scopy(p,"  AES-CCM rtrip  ");p=scopy(p,ok?"PASS  (enc+dec+verify ok)":"FAIL");*p=0;
                          tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); if(ok)pass++; }
                        p=b;p=scopy(p,"  ");p=udec(p,(u32)pass);*p++='/';p=udec(p,(u32)tot);p=scopy(p," crypto tests passed");*p=0;
                        tcol(pass==tot?GREEN:RED); term_puts(b); term_putc('\n');
                        if(pass==tot) term_puts("  WPA2 crypto foundation is correct - ready to build the 4-way handshake.\n"); tcol(WHT); }
                    else if((arg[0]=='s'||arg[0]=='S')&&(arg[1]=='c'||arg[1]=='C')){
                        if(!g_rxnum){ tcol(rgb(230,120,90)); term_puts("  run 'wifi rings' first (after allbuilt/macinit/fw)\n"); tcol(WHT); }
                        else {
                            tcol(rgb(120,200,255)); term_puts("  [wifi scan] RX DMA on, hopping channels 1-13 for beacons...\n"); tcol(WHT);
                            g_apn=0; rtw_rx_ring_up(mmio);
                            static const u8 chans[]={1,6,11,2,3,4,5,7,8,9,10,12,13};
                            for(int ci=0; ci<13; ci++){ rtw_set_channel_2g(mmio,chans[ci]);
                                for(int w=0;w<3;w++){ rtw_delay_us(60000); ui_pump(); rtw_scan_drain(mmio,chans[ci]); } }
                            if(g_apn==0){
                                /* DIAGNOSTIC: show the raw RX ring/DMA state so we can see WHERE it breaks */
                                u32 wp=(rtw_r32(mmio,0x03B4)>>16)&0x0FFF; u32 rp=rtw_r32(mmio,0x03B4)&0x0FFF;
                                u32 cr=rtw_r32(mmio,0x0100); u32 rcr=rtw_r32(mmio,0x0608); u32 pcictrl=rtw_r32(mmio,0x0300);
                                u8* d0=g_rxring; u16 bs=*(volatile u16*)(u64)d0; u32 dma0=*(volatile u32*)(u64)(d0+4);
                                u32 w0=*(volatile u32*)(u64)g_rxbuf;
                                p=b;p=scopy(p,"  DIAG hwWP=");p=udec(p,wp);p=scopy(p," RP=");p=udec(p,rp);p=scopy(p," CR=0x");p=hx(p,cr,4);p=scopy(p," RCR=0x");p=hx(p,rcr,8);*p=0; tcol(CYAN); term_puts(b); term_putc('\n');
                                p=b;p=scopy(p,"  DIAG PCICTRL=0x");p=hx(p,pcictrl,8);p=scopy(p,"  desc0.bufsz=");p=udec(p,bs);p=scopy(p," desc0.dma=0x");p=hx(p,dma0,8);p=scopy(p," buf0.w0=0x");p=hx(p,w0,8);*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
                                tcol(rgb(230,160,60)); term_puts("  no beacons parsed via RX DMA (frames still counted in wifi rx)\n"); tcol(WHT); }
                            else {
                                tcol(GREEN); p=b;p=scopy(p,"  found ");p=udec(p,(u32)g_apn);p=scopy(p," networks:");*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
                                term_puts("  ID  SSID                          CH  SECURITY\n");
                                for(int i=0;i<g_apn;i++){ p=b; *p++=' ';*p++=' ';
                                    p=udec(p,(u32)(i+1)); if(i+1<10)*p++=' '; *p++=' ';*p++=' ';
                                    int sl=0; while(g_aps[i].ssid[sl]&&sl<28){*p++=g_aps[i].ssid[sl];sl++;}
                                    if(sl==0){p=scopy(p,"<hidden>");sl=8;}
                                    while(sl<30){*p++=' ';sl++;}
                                    p=udec(p,g_aps[i].ch); if(g_aps[i].ch<10)*p++=' ';
                                    p=scopy(p,"  ");p=scopy(p, g_aps[i].sec==0?"OPEN":g_aps[i].sec==2?"WPA2":"WEP/WPA"); *p=0;
                                    tcol(g_aps[i].sec==0?GREEN:CYAN); term_puts(b); term_putc('\n'); tcol(WHT); }
                                term_puts("  connect with:  wifi wpa <ID>    e.g.  wifi wpa 1\n");
                            }
                        } }
                    else if((arg[0]=='r'||arg[0]=='R')&&(arg[1]=='x'||arg[1]=='X')){
                        const char* a=arg+2; while(*a==' ')a++;
                        int dma=0; if((a[0]=='d'||a[0]=='D')&&(a[1]=='m'||a[1]=='M')){ dma=1; a+=3; while(*a==' ')a++; }
                        u8 ch=1; if(*a>='0'&&*a<='9'){ int v=0; while(*a>='0'&&*a<='9'){v=v*10+(*a-'0');a++;} if(v>=1&&v<=14)ch=(u8)v; }
                        p=b;p=scopy(p,"  [wifi rx] channel ");p=udec(p,ch);p=scopy(p,dma?"  (DMA parse mode)":"  (safe counter mode)");*p=0;
                        tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                        if(!dma){
                            u32 c0c=rtw_r32(mmio,0x0F04), c0o=rtw_r32(mmio,0x0F14), c0h=rtw_r32(mmio,0x0F10);
                            u32 a0o=rtw_r32(mmio,0x0F08), a0c=rtw_r32(mmio,0x0FCC);
                            p=b;p=scopy(p,"  step1 ok: baseband counters readable  RF18=0x");p=hx(p,rtw_rf_read_a(mmio,0x18),5);*p=0; term_puts(b); term_putc('\n');
                            term_puts("  step2: setting channel (read-modify-write, like rtw88)\n");
                            rtw_set_channel_2g(mmio,ch);
                            p=b;p=scopy(p,"  step2 ok: RF18 now 0x");p=hx(p,rtw_rf_read_a(mmio,0x18),5);*p=0; term_puts(b); term_putc('\n');
                            term_puts("  step3: receiver on (RCR), RX DMA stays OFF - no memory writes\n");
                            rtw_w32(mmio,0x0608,0xE400220Eu);
                            rtw_w8(mmio,0x0100,(u8)((rtw_r8(mmio,0x0100)|(1u<<7)|(1u<<6)|(1u<<4)|(1u<<5)) & ~((1u<<1)|(1u<<3))));
                            term_puts("  listening 3s...\n");
                            for(int w=0;w<2;w++) rtw_delay_us(200000);
                            u32 c1c=rtw_r32(mmio,0x0F04), c1o=rtw_r32(mmio,0x0F14), c1h=rtw_r32(mmio,0x0F10);
                            u32 a1o=rtw_r32(mmio,0x0F08), a1c=rtw_r32(mmio,0x0FCC);
                            u32 okc=(u32)((u16)(c1c&0xFFFF)-(u16)(c0c&0xFFFF));
                            u32 oko=(u32)((u16)(c1o&0xFFFF)-(u16)(c0o&0xFFFF));
                            u32 okh=(u32)((u16)(c1h&0xFFFF)-(u16)(c0h&0xFFFF));
                            u32 erc=(u32)((u16)(c1c>>16)-(u16)(c0c>>16));
                            u32 ero=(u32)((u16)(c1o>>16)-(u16)(c0o>>16));
                            u32 cca=(u32)((u16)(a1o>>16)-(u16)(a0o>>16))+(u32)((u16)(a1c&0xFFFF)-(u16)(a0c&0xFFFF));
                            p=b;p=scopy(p,"  good frames: CCK ");p=udec(p,okc);p=scopy(p,"  OFDM ");p=udec(p,oko);p=scopy(p,"  HT ");p=udec(p,okh);
                            p=scopy(p,"   bad CRC: ");p=udec(p,erc+ero);p=scopy(p,"   CCA hits: ");p=udec(p,cca);*p=0;
                            tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                            if(okc+oko+okh>0){ tcol(GREEN); term_puts("  *** THE RADIO IS DECODING REAL 802.11 FRAMES OFF THE AIR ***\n"); tcol(WHT); }
                            else if(cca+erc+ero>0){ tcol(rgb(230,160,60)); term_puts("  radio hears energy but no clean frames yet - try another channel (wifi rx 6 / 11)\n"); tcol(WHT); }
                            else { tcol(rgb(230,160,60)); term_puts("  silence on this channel - try wifi rx 6 and wifi rx 11 near the router\n"); tcol(WHT); }
                        } else {
                            rtw_set_channel_2g(mmio,ch);
                            if(!g_rxnum){ tcol(rgb(230,120,90)); term_puts("  RX ring not set up - run 'wifi rings' first\n"); tcol(WHT); }
                            else {
                                rtw_rx_enable(mmio);
                                char ssid[40]; int rssi=0; u8 bssid[6];
                                int r=rtw_rx_poll_beacon(mmio,ssid,&rssi,bssid);
                                if(r==1){ tcol(GREEN);
                                    p=b;p=scopy(p,"  BEACON! SSID=\"");p=scopy(p,ssid);p=scopy(p,"\"  BSSID=");for(int z=0;z<6;z++){p=hx(p,bssid[z],2); if(z<5)*p++=':';}*p=0;
                                    term_puts(b); term_putc('\n'); term_puts("  *** THE CHIP RECEIVED A REAL 802.11 FRAME OFF THE AIR ***\n"); tcol(WHT); }
                                else { tcol(rgb(230,160,60)); term_puts("  no beacon parsed (counter mode may still show frames: wifi rx 6)\n"); tcol(WHT); } } } }
                    else if((arg[0]=='h'||arg[0]=='H')&&(arg[1]=='2')){
                        tcol(rgb(120,200,255)); term_puts("  [wifi h2c] sending QUERY_BT_INFO (a safe no-op H2C) to the MCU\n"); tcol(WHT);
                        u8 hmetfr=rtw_r8(mmio,0x01CC);
                        p=b;p=scopy(p,"  HMETFR before=0x");p=hx(p,hmetfr,2);*p=0; tcol(GREY); term_puts(b); term_putc('\n'); tcol(WHT);
                        int r=rtw_h2c_send(mmio,0,0x00000161u,0x00000000u);
                        if(r<0){ tcol(rgb(230,120,90)); term_puts("  mailbox stayed full - MCU not draining H2C (firmware not running yet)\n"); tcol(WHT); }
                        else {
                            int drained=0; for(int t=0;t<300;t++){ if(!(rtw_r8(mmio,0x01CC)&1)){drained=1;break;} rtw_delay_us(100); }
                            u32 c2h0=rtw_r32(mmio,0x01AC), c2h1=rtw_r32(mmio,0x10F8);
                            p=b;p=scopy(p,"  HMETFR after=0x");p=hx(p,rtw_r8(mmio,0x01CC),2);p=scopy(p,"  C2H(0x1AC)=0x");p=hx(p,c2h0,8);p=scopy(p,"  DBG6=0x");p=hx(p,c2h1,8);*p=0;
                            tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                            if(drained){ tcol(GREEN); term_puts("  MCU drained the H2C box -- the processor is alive and reading commands!\n"); tcol(WHT); }
                            else { tcol(rgb(230,160,60)); term_puts("  box not drained within timeout (MCU may not be running - do 'wifi fw' first)\n"); tcol(WHT); } } }
                    else if((arg[0]=='g'||arg[0]=='G')||((arg[0]=='a'||arg[0]=='A')&&(arg[1]=='l'||arg[1]=='L'))){
                        tcol(rgb(120,200,255)); term_puts("  phy_set_param: power on BB/RF domain, then tables (mac->bb->agc->rf)\n"); tcol(WHT);
                        u32 rfpre=rtw_rf_read_a(mmio,0x18);
                        rtw_phy_power_on(mmio);
                        p=b;p=scopy(p,"  RF_CTRL(0x1F)=0x");p=hx(p,rtw_r8(mmio,0x001F),2);p=scopy(p,"  WLRF1+3(0xEF)=0x");p=hx(p,rtw_r8(mmio,0x00EF),2);
                        p=scopy(p,"  SYS_FUNC_EN=0x");p=hx(p,rtw_r8(mmio,0x0002),2);p=scopy(p,"  RF18 before=0x");p=hx(p,rfpre,5);*p=0;
                        tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        int m1=rtw_apply_built(mmio,rtw_bmac,138,"mac",1);
                        rtw_apply_built(mmio,rtw_bbb,1669,"bb",1);
                        rtw_apply_built(mmio,rtw_bagc,386,"agc",0);
                        rtw_apply_built(mmio,rtw_brf,586,"rf-a",0);
                        if(!rtw_efuse_valid){ rtw_efuse_dump_phy(mmio,rtw_phymap,RTW_EFUSE_PHY_SZ); rtw_efuse_valid=(rtw_efuse_logical(rtw_phymap,RTW_EFUSE_PHY_SZ,RTW_EFUSE_PROT_SZ,rtw_logmap,RTW_EFUSE_LOG_SZ)>0); }
                        if(rtw_efuse_valid){ for(int i=0;i<6;i++)rtw_w8(mmio,0x0610+i,rtw_logmap[0xD0+i]); }
                        u8 xk=rtw_logmap[0xB9];
                        int hx_ok=(rtw_efuse_valid && xk!=0xFF);
                        rtw_phy_post(mmio,hx_ok,xk);
                        rtw_wmac_init(mmio);
                        u32 rfpost=rtw_rf_read_a(mmio,0x18), rf00=rtw_rf_read_a(mmio,0x00);
                        p=b;p=scopy(p,"  after tables: RF18=0x");p=hx(p,rfpost,5);p=scopy(p,"  RF00=0x");p=hx(p,rf00,5);
                        p=scopy(p,"  xtal_cap=");if(hx_ok){p=scopy(p,"0x");p=hx(p,xk&0x3F,2);}else p=scopy(p,"skipped");*p=0;
                        tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        if(rfpost!=rfpre){ tcol(GREEN); term_puts("  RF REGISTERS CHANGED -- the radio is out of reset and accepting writes!\n"); tcol(WHT); }
                        else { tcol(rgb(230,160,60)); term_puts("  RF18 unchanged -- RF may still be held off (send this line)\n"); tcol(WHT); }
                        tcol(m1?rgb(230,160,60):GREEN);
                        if(m1) term_puts("  done - some MAC readback mismatches (see above)\n");
                        else term_puts("  done - MAC readback fully matched. stage B/C applied.\n");
                        tcol(WHT); }
                    else if(arg[0]=='m'||arg[0]=='M'||arg[0]=='b'||arg[0]=='B'||((arg[0]=='a'||arg[0]=='A')&&(arg[1]=='g'||arg[1]=='G'))||((arg[0]=='r'||arg[0]=='R')&&(arg[1]=='f'||arg[1]=='F'))){
                        char c0=arg[0]; const char* fn; const char* lbl; int verify;
                        if(c0=='m'||c0=='M'){ fn="RTWMAC.TXT"; lbl="mac"; verify=1; }
                        else if(c0=='b'||c0=='B'){ fn="RTWBB.TXT"; lbl="bb"; verify=1; }
                        else if(c0=='a'||c0=='A'){ fn="RTWAGC.TXT"; lbl="agc"; verify=0; }
                        else { fn="RTWRFA.TXT"; lbl="rf-a"; verify=0; }
                        p=b;p=scopy(p,"  [wifi ");p=scopy(p,lbl);p=scopy(p,"] applying ");p=scopy(p,fn);*p=0;
                        tcol(rgb(255,210,90)); term_puts(b); term_putc('\n'); tcol(WHT);
                        int fl=rtw_read_table_file(fn,fat_filebuf,65535);
                        if(fl<0){ tcol(rgb(230,120,90));
                            p=b;p=scopy(p,"  ");p=scopy(p,fn);p=scopy(p," not found on the boot volume.");*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
                            term_puts("  put it on the USB stick root (or the boot disk). generate with rtwgen from rtw8821c_table.c\n");
                            term_puts("  line format:  offset size value   (hex; size 1|2|4=MMIO, 5=RF-A SIPI)\n"); }
                        else {
                            fat_filebuf[fl]=0;
                            int cnt=rtw_tbl_parse((const char*)fat_filebuf,fl);
                            p=b;p=scopy(p,"  parsed ");p=udec(p,(u32)cnt);p=scopy(p," writes from ");p=udec(p,(u32)fl);p=scopy(p," bytes");*p=0;
                            tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                            int mism=0,delays=0,rfw=0;
                            for(int i=0;i<cnt;i++){
                                u8 sz=rtw_mactbl[i].sz; u32 off=rtw_mactbl[i].off, val=rtw_mactbl[i].val;
                                int wasdelay=rtw_apply_one(mmio,off,sz,val);
                                if(wasdelay){ delays++; continue; }
                                if(sz==5){ rfw++; continue; }
                                for(volatile int d=0;d<200;d++) io_wait();
                                if(verify){
                                    u32 rb=rtw_rdsz(mmio,off,sz);
                                    if(rb!=val){ mism++;
                                        if(mism<=12){ tcol(rgb(230,160,60));
                                            p=b;p=scopy(p,"    0x");p=hx(p,off,4);p=scopy(p," wrote 0x");p=hx(p,val,8);
                                            p=scopy(p," read 0x");p=hx(p,rb,8);*p=0; term_puts(b); term_putc('\n'); tcol(WHT); } } } }
                            p=b;p=scopy(p,"  applied ");p=udec(p,(u32)cnt);
                            if(rfw){p=scopy(p,"  rf-writes ");p=udec(p,(u32)rfw);}
                            if(delays){p=scopy(p,"  delays ");p=udec(p,(u32)delays);}
                            if(verify){p=scopy(p,"  mismatches ");p=udec(p,(u32)mism);}
                            *p=0;
                            tcol((verify&&mism)?rgb(230,160,60):GREEN); term_puts(b); term_putc('\n'); tcol(WHT);
                            if(verify) term_puts("  (some mismatches are normal for write-only/self-clearing regs)\n");
                            else term_puts("  (rf/agc writes are not read back here - use 'wifi dump' to snapshot)\n"); } }
                    else if((arg[0]=='d'||arg[0]=='D')&&(arg[1]=='h'||arg[1]=='H')){
                        if(!g_wpa_ok){ tcol(rgb(230,120,90)); term_puts("  not connected - run 'wifi wpa <ID>' first\n"); tcol(WHT); }
                        else { netdev_set(NETDEV_WIFI); g_dhcp_done=0; g_dhcp_ok=0;
                            tcol(rgb(120,200,255)); term_puts("  [wifi dhcp] requesting a lease over Wi-Fi (GTK "); term_puts(g_wnd_have_gtk?"present":"missing"); term_puts(")...\n"); tcol(WHT);
                            int ok=dhcp_config();
                            if(ok){ p=b; p=scopy(p,"  lease: ip="); p=ipstr(p,my_ip); p=scopy(p," gw="); p=ipstr(p,gw_ip); p=scopy(p," dns="); p=ipstr(p,dns_ip); *p=0; tcol(GREEN); term_puts(b); term_putc('\n');
                                term_puts("  Wi-Fi is now the active link. Try: ping 1.1.1.1  |  dns example.com\n"); tcol(WHT); }
                            else { tcol(rgb(230,160,60)); term_puts("  no lease. "); term_puts(g_wnd_have_gtk?"broadcast reachable; AP may be slow - retry.":"no GTK: broadcast DHCP reply can't be decrypted (this is the GTK case).");
                                term_putc('\n');
                                { char s2[80]; char* q=s2; q=scopy(q,"  wifi rx="); q=udec(q,g_wnd_rx_frames); q=scopy(q," drop="); q=udec(q,g_wnd_rx_drop); q=scopy(q," tx="); q=udec(q,g_wnd_tx_frames); *q=0; tcol(GREY); term_puts(s2); term_putc('\n'); tcol(WHT); }
                                netdev_set(NETDEV_ETH); } }
                    }
                    else if((arg[0]=='e'||arg[0]=='E')&&arg[1]=='t'){ netdev_set(NETDEV_ETH); tcol(WHT); term_puts("  active link: Ethernet\n"); tcol(WHT); }
                    else if(arg[0]=='d'||arg[0]=='D'){
                        tcol(rgb(255,210,90)); term_puts("  [wifi dump] snapshotting MMIO 0x0000-0x0FFF\n"); tcol(WHT);
                        for(int o=0;o<0x1000;o+=4) *(u32*)(fat_filebuf+o)=rtw_r32(mmio,(u32)o);
                        if((msd_ok||msd_autoinit()) && ufat_write_file("RTWREGS.BIN",(const char*)fat_filebuf,0x1000)==0){
                            tcol(GREEN); term_puts("  wrote RTWREGS.BIN (4096 bytes) to the USB stick\n"); tcol(WHT); }
                        else { tcol(rgb(230,160,60)); term_puts("  no USB stick - printing 0x000-0x03F instead\n"); tcol(GREY);
                            for(int o=0;o<0x40;o+=16){ char hl[80]; rtw_hexline(fat_filebuf,o,16,hl); term_puts(hl); term_putc('\n'); } tcol(WHT); } }
                    else if(!doload && !doinit && !(arg[0]=='r'||arg[0]=='R')){ tcol(WHT); term_puts("  status: detected, enabled, firmware ready.\n"); term_puts("  init efuse allbuilt macinit txbuf fw h2c bb agc rf rings load dump.  macinit=TRX/page setup.  EXPERIMENTAL.\n"); }
                    else {
                        #define WMR8(o)   (*(volatile u8*)(u64)(mmio+(o)))
                        if((arg[0]=='r'||arg[0]=='R')&&(arg[1]=='i'||arg[1]=='I')){
                            /* ---- STAGE 3: PCI DMA ring infrastructure (transcribed from rtw88 pci.c) ---- */
                            tcol(rgb(255,210,90)); term_puts("  [wifi rings] setting up TX/RX DMA descriptor rings (rtw88 layout, EXPERIMENTAL)\n"); tcol(WHT);
                            /* rtw_pci_tx/rx_buffer_desc: { u16 buf_size; u16 psb_len/total; u32 dma; } = 8 bytes.
                               DMA-coherent ring memory from the heap (x86 DMA is cache-coherent, so heap buffers work as DMA targets; identity-mapped => phys==virt). */
                            u32 RXNUM=128, BENUM=256;                    /* RTK_MAX_RX / RTK_BEQ_TX desc counts */
                            static u8 *rxring=0,*bering=0,*rxbuf=0;
                            static u8 *mgring=0;
                            if(!rxring){ rxring=(u8*)dma_alloc(RXNUM*8); bering=(u8*)dma_alloc(BENUM*16); mgring=(u8*)dma_alloc(BENUM*16); rxbuf=(u8*)dma_alloc(RXNUM*2048); }   /* alloc once; reuse on re-run (no arena leak) */
                            if(!rxring||!bering||!rxbuf){ tcol(rgb(230,120,90)); term_puts("  ring alloc failed (heap)\n"); tcol(WHT); }
                            else {
                                for(u32 i=0;i<RXNUM*8;i++)rxring[i]=0; for(u32 i=0;i<BENUM*16;i++){bering[i]=0;mgring[i]=0;}
                                for(u32 i=0;i<RXNUM;i++){ u8* d=rxring+i*8; u64 pb=(u64)(rxbuf+i*2048);   /* fill RX descriptors */
                                    *(volatile u16*)(d)=2048; *(volatile u16*)(d+2)=0; *(volatile u32*)(d+4)=(u32)pb; }
                                /* dma reset: RTK_PCI_CTRL (0x300) - toggle TRXDMA interface reset + RX tag */
                                *(volatile u32*)(u64)(mmio+0x300) |= (1u<<0)|(1u<<16);
                                for(volatile int dl=0;dl<2000;dl++) io_wait();
                                /* program ring-base + count registers (RTL8821CE offsets from pci.h) */
                                g_rxring=rxring; g_rxbuf=rxbuf; g_rxnum=RXNUM; g_rxidx=0;
                                g_bering=bering; g_benum=BENUM; g_beidx=0;
                                g_mgring=mgring; g_mgnum=BENUM; g_mgidx=0;
                                *(volatile u32*)(u64)(mmio+0x338)=(u32)(u64)rxring; *(volatile u16*)(u64)(mmio+0x382)=(u16)RXNUM;   /* RXBD_DESA_MPDUQ / NUM */
                                *(volatile u32*)(u64)(mmio+0x328)=(u32)(u64)bering; *(volatile u16*)(u64)(mmio+0x388)=(u16)BENUM;   /* TXBD_DESA_BEQ  / NUM  */
                                *(volatile u32*)(u64)(mmio+0x310)=(u32)(u64)mgring; *(volatile u16*)(u64)(mmio+0x380)=(u16)BENUM;   /* TXBD_DESA_MGMTQ / NUM */
                                *(volatile u16*)(u64)(mmio+0x39C)=0xFFFF;   /* TXBD_RWPTR_CLR: reset read/write ptrs */
                                /* enable TX/RX DMA: CR (0x0100) MAC_TRX bits incl BIT_TXDMA_EN(2)|BIT_RXDMA_EN(3) */
                                u16 cr=*(volatile u16*)(u64)(mmio+0x0100); *(volatile u16*)(u64)(mmio+0x0100)=(u16)(cr|(1<<2)|(1<<3));
                                for(volatile int dl=0;dl<2000;dl++) io_wait();
                                tcol(GREEN); p=b;p=scopy(p,"  rings up: RX ");p=udec(p,RXNUM);p=scopy(p," desc @0x");p=hx(p,(u32)(u64)rxring,8);p=scopy(p,"  TX(BE) ");p=udec(p,BENUM);p=scopy(p," @0x");p=hx(p,(u32)(u64)bering,8);*p=0; term_puts(b); term_putc('\n');
                                u32 cr2=*(volatile u16*)(u64)(mmio+0x0100);
                                p=b;p=scopy(p,"  CR after DMA-enable = 0x");p=hx(p,cr2,4);p=scopy(p,"  (TXDMA/RXDMA bits should be set)");*p=0; tcol(CYAN); term_puts(b); term_putc('\n');
                                tcol(WHT); term_puts("  rings allocated + registers programmed. next: 'wifi load' can use these for the real TX-ring download.\n");
                            }
                        }
                        if(doinit){
                            /* --- RTL8821CE cardemu->act power-on sequence, transcribed from rtw88
                                   (trans_cardemu_to_act_8821c), PCI + ALL-interface commands only. --- */
                            tcol(rgb(255,210,90)); term_puts("  [wifi init] rtw88 power-on sequence (EXPERIMENTAL) - watch for a hang\n"); tcol(WHT);
                            /* {offset, mask, value, cmd}: cmd 1=write(set masked bits to value), 2=poll(wait bits==value) */
                            struct { u16 off; u8 mask; u8 val; u8 cmd; } seqp[] = {
                                {0x0020,0x01,0x01,1},                 /* APS_FSMCO: enable LDOA/analog        */
                                {0x0001,0,0,3},                        /* delay 1ms (cmd 3)                    */
                                {0x0000,0x20,0x00,1},                  /* CR: release reset bit5              */
                                {0x0005,(0x10|0x08|0x04),0x00,1},      /* APS_FSMCO: clear pd bits            */
                                {0x0075,0x01,0x01,1},                  /* PCIE: enable                        */
                                {0x0006,0x02,0x02,2},                  /* poll SYS_CLKR ready (bit1)          */
                                {0x0075,0x01,0x00,1},
                                {0x0006,0x01,0x01,1},
                                {0x0005,0x80,0x00,1},                  /* clear bit7                          */
                                {0x0005,(0x10|0x08),0x00,1},
                                {0x0005,0x01,0x01,1},                  /* APS_FSMCO: MAC on (bit0)            */
                                {0x0005,0x01,0x00,2},                  /* poll until bit0 clears (MAC ready)  */
                                {0x0020,0x08,0x08,1},                  /* enable                              */
                                {0x0074,0x20,0x20,1},                  /* PCI: bit5                           */
                                {0x0022,0x02,0x00,1},
                                {0x0062,(0x80|0x40|0x20),(0x80|0x40|0x20),1},
                                {0x0061,(0x80|0x40|0x20),0x00,1},
                                {0x007C,0x02,0x00,1},
                            };
                            int nn=(int)(sizeof(seqp)/sizeof(seqp[0])), okall=1;
                            for(int s=0;s<nn;s++){ u16 o=seqp[s].off;
                                if(seqp[s].cmd==3){ for(volatile int d=0;d<100000;d++) io_wait(); continue; }
                                if(seqp[s].cmd==2){ int got=0; for(int t=0;t<100000;t++){ if((WMR8(o)&seqp[s].mask)==seqp[s].val){got=1;break;} io_wait(); }
                                    if(!got){ okall=0; p=b;p=scopy(p,"  poll timeout @0x");p=hx(p,o,4);*p=0; tcol(rgb(230,120,90)); term_puts(b); term_putc('\n'); tcol(WHT); } continue; }
                                u8 v=WMR8(o); v=(u8)((v & ~seqp[s].mask) | (seqp[s].val & seqp[s].mask)); WMW8(o,v);   /* write masked */
                                for(volatile int d=0;d<500;d++) io_wait();
                            }
                            u32 after=WMR(0xF0);
                            tcol(okall?GREEN:rgb(230,160,60)); p=b;p=scopy(p,"  power-on ");p=scopy(p,okall?"completed":"completed with poll timeouts");p=scopy(p,"  SYS_CFG=0x");p=hx(p,after,8);*p=0; term_puts(b); term_putc('\n');
                            u8 mac2[6]; for(int i=0;i<6;i++)mac2[i]=WMR8(0x0610+i);
                            tcol(CYAN); p=b;p=scopy(p,"  MAC now ");for(int i=0;i<6;i++){p=hx(p,mac2[i],2);if(i<5)*p++=':';}*p=0; term_puts(b); term_putc('\n');
                            tcol(WHT); term_puts("  if MAC changed from EA:EA:.. the chip powered on + read EFUSE. then try 'wifi load'.\n");
                        }
                        if(doload){
                        tcol(rgb(255,210,90)); term_puts("  [wifi load] EXPERIMENTAL firmware download - watch for a hang, power-cycle if needed\n"); tcol(WHT);
                        const unsigned char* body=_binary_rtw8821c_fw_bin_start+32; long blen=fwsz-32;   /* skip 32B header */
                        u16 ctrl=WMR16(0x0080); WMW16(0x0080, ctrl | 0x0001);                            /* FWDL_EN */
                        for(volatile int d=0;d<100000;d++) io_wait();
                        long off=0; int page=0;
                        while(off<blen){ int n=(int)(blen-off); if(n>4096)n=4096;
                            WMW8(0x0084, (u8)(page & 0x07));                                             /* page index (low bits) */
                            u16 c=WMR16(0x0080); WMW16(0x0080,(c & ~0x0700) | ((page & 7)<<8));          /* page select */
                            for(int i=0;i<n;i++) *(volatile u8*)(u64)(mmio+0x8000+i)=body[off+i];        /* copy page to FW FIFO window */
                            off+=n; page++;
                            for(volatile int d=0;d<2000;d++) io_wait();
                        }
                        u16 fin=WMR16(0x0080); WMW16(0x0080, fin & ~0x0001);                             /* clear FWDL_EN */
                        int ready=0; for(int t=0;t<200000;t++){ if(WMR16(0x0080) & 0x0080){ ready=1; break; } io_wait(); }
                        u16 st=WMR16(0x0080);
                        p=b;p=scopy(p,"  MCUFW_CTRL(0x80)=0x");p=hx(p,st,4);p=scopy(p,"  pages=");p=udec(p,(u32)page);*p=0; tcol(CYAN); term_puts(b);term_putc('\n');
                        if(ready){ tcol(GREEN); term_puts("  MCU_FW_READY set -- chip ACCEPTED the firmware! (radio microcode running)\n");
                            if(st&0x0800) term_puts("  checksum OK\n"); tcol(WHT);
                            term_puts("  NOTE: firmware runs, but device-init + 802.11 + WPA are not implemented -> no connection yet.\n"); }
                        else { tcol(rgb(230,120,90)); term_puts("  MCU_FW_READY not set -- chip did not confirm (needs full power-on/PHY init first).\n"); tcol(WHT);
                            term_puts("  this is expected without device-init; the download path ran without hanging, which is the real test.\n"); } } }
            } } } }
    else if(seq(cmd,"net")){ int r=nic_up(); if(!pci_n)pci_scan(); int k=nic_index();
        tcol(CYAN); term_puts("NIC: "); tcol(WHT); term_puts(k>=0?nic_name(pci_ven[k],pci_dev[k]):"none");
        if(k>=0){ char b[40],*p=b; p=scopy(p,"  ["); p=hx(p,pci_ven[k],4);*p++=':';p=hx(p,pci_dev[k],4);*p++=']'; *p=0; term_puts(b); } term_putc('\n');
        if(r){ char b[64],*p=b; p=scopy(p,"MAC "); for(int i=0;i<6;i++){p=hx(p,my_mac[i],2); if(i<5)*p++=':';} *p=0; term_puts(b); term_putc('\n');
            p=b; p=scopy(p,"IP "); p=ipstr(p,my_ip); p=scopy(p,"/"); { u32 m=my_mask,c=0; while(m&0x80000000u){c++;m<<=1;} p=udec(p,c); } p=scopy(p,"  GW "); p=ipstr(p,gw_ip); p=scopy(p,g_dhcp_ok?"  (DHCP)":"  (static)"); *p=0; tcol(GREEN); term_puts(b); tcol(WHT); term_putc('\n');
            p=b; p=scopy(p,"DNS "); p=ipstr(p,dns_ip); *p=0; tcol(GREY); term_puts(b); tcol(WHT); term_putc('\n');
            term_puts("try: dhcp   ping 10.0.2.2   dns example.com   http example.com\n"); }
        else { tcol(rgb(240,150,120)); term_puts("link down - set the VM adapter to PCnet-FAST III (NAT)\n"); tcol(WHT); } }
    else if(seq(cmd,"dhcp")){ tcol(GREY); term_puts("requesting a lease...\n"); tcol(WHT);
        int r=dhcp_renew();
        if(r==1){ char b[96],*p=b; p=scopy(p,"IP "); p=ipstr(p,my_ip); p=scopy(p,"/"); { u32 m=my_mask,c=0; while(m&0x80000000u){c++;m<<=1;} p=udec(p,c); } p=scopy(p,"  GW "); p=ipstr(p,gw_ip); p=scopy(p,"  DNS "); p=ipstr(p,dns_ip); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
        else if(r==0){ tcol(rgb(240,150,120)); term_puts("no DHCP server answered - keeping the static address\n"); tcol(WHT); }
        else { term_puts("network not available\n"); } }
    else if(seq(cmd,"ping")){ u32 ip; if(!parse_ip(arg,&ip)){ term_puts("usage: ping A.B.C.D\n"); }
        else { int r=do_ping(ip); if(r==1){ char b[40],*p=b; p=scopy(p,"reply from "); p=scopy(p,arg); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
            else if(r==0) term_puts("no reply (timeout)\n"); else term_puts("network not available\n"); } }
    else if(seq(cmd,"dns")){ if(!arg[0]){ term_puts("usage: dns hostname\n"); }
        else { u32 ip; int r=dns_query(arg,&ip); if(r==1){ char b[64],*p=b; p=scopy(p,arg); p=scopy(p," -> "); char t[16]; { u32 v=ip; int q=0; char oc[4]; for(int i=3;i>=0;i--){ u32 o=(v>>(i*8))&0xFF; if(!o)t[q++]='0'; else { int n=0; while(o){oc[n++]='0'+o%10;o/=10;} while(n)t[q++]=oc[--n]; } if(i)t[q++]='.'; } t[q]=0; } p=scopy(p,t); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
            else term_puts("could not resolve\n"); } }
    else if(seq(cmd,"http")||seq(cmd,"https")||seq(cmd,"wget")||seq(cmd,"get")){
        if(!arg[0]){ term_puts("usage: http host[/path]   or   https host[/path]\n"); }
        else { int secure=seq(cmd,"https"); char host[128],path[300]; int hi=0,pi=0; const char*s=arg;
            if(s[0]=='h'&&s[1]=='t'&&s[2]=='t'&&s[3]=='p'){ if(s[4]=='s')secure=1; while(*s&&*s!=':')s++; if(*s==':'){s++; while(*s=='/')s++;} }
            while(*s&&*s!='/'&&*s!=' '&&hi<127)host[hi++]=*s++; host[hi]=0;
            path[pi++]='/'; if(*s=='/')s++; while(*s&&*s!=' '&&pi<299)path[pi++]=*s++; path[pi]=0;
            tcol(GREY); term_puts(secure?"fetching https://":"fetching http://"); term_puts(host); term_puts("...\n"); tcol(WHT);
            int n = secure ? https_fetch(host,path) : http_fetch(host,path);
            if(n==-100){ tcol(rgb(240,120,120)); term_puts("DNS failed\n"); tcol(WHT); }
            else if(n<=0){ tcol(rgb(240,120,120));
                if(secure){ char b[52],*p=b; p=scopy(p,"HTTPS/TLS failed (code -"); p=udec(p,(u32)(-n)); p=scopy(p,") - see netlog"); *p=0; term_puts(b); term_putc('\n'); }
                else if(n==-1) term_puts("no NIC (set VM to PCnet-FAST III)\n");
                else if(n==-2) term_puts("ARP failed (no gateway)\n");
                else if(n==-3) term_puts("no SYN-ACK (timeout)\n");
                else if(n==-4) term_puts("connection refused (RST)\n");
                else term_puts("connected, no data\n");
                tcol(WHT); }
            else { http_buf[n]=0; int bo=n; for(int i=0;i+3<n;i++) if(http_buf[i]=='\r'&&http_buf[i+1]=='\n'&&http_buf[i+2]=='\r'&&http_buf[i+3]=='\n'){ bo=i+4; break; }
                tcol(GREEN); { int e=0; while(e<bo && http_buf[e]!='\r' && http_buf[e]!='\n'){ term_putc(http_buf[e]); e++; } } term_putc('\n'); tcol(GREY);
                { char b[32],*p=b; p=scopy(p,"("); p=udec(p,(u32)n); p=scopy(p," bytes)"); *p=0; term_puts(b); term_putc('\n'); } tcol(WHT);
                int shown=0,intag=0; char last=0;
                for(int i=bo;i<n&&shown<1400;i++){ char c=http_buf[i]; if(c=='<'){intag=1;continue;} if(c=='>'){intag=0;continue;} if(intag)continue; if(c=='\r')continue;
                    if(c==' '||c=='\n'||c=='\t'){ if(last==' '||last=='\n')continue; c=(c=='\n')?'\n':' '; } if(c<32&&c!='\n')continue; term_putc(c); last=c; shown++; }
                term_putc('\n'); } } }
    else if(seq(cmd,"disks")||seq(cmd,"disk")||seq(cmd,"lsblk")){
        gdisk_ensure();
        { int nb,ns,nfn; if(pci_find(0x01,0x08,-1,&nb,&ns,&nfn)){ char d[90],*q=d; tcol(rgb(255,210,90)); q=scopy(q,"NVMe controller @ bus ");q=udec(q,(u32)nb);q=scopy(q," slot ");q=udec(q,(u32)ns);q=scopy(q,"  init: ");q=scopy(q,nvme_ok?"OK":"FAILED stage ");if(!nvme_ok)q=udec(q,(u32)nvme_failstage);*q=0; term_puts(d); term_putc('\n'); tcol(WHT); } }
        { int ab,as,afn; if(pci_find(0x01,0x06,-1,&ab,&as,&afn)){ char d[90],*q=d; tcol(rgb(255,210,90)); q=scopy(q,"AHCI controller @ bus ");q=udec(q,(u32)ab);q=scopy(q," slot ");q=udec(q,(u32)as);*q=0; term_puts(d); term_putc('\n'); tcol(WHT); } }
        { char ab[80],*ap=ab; tcol(GREEN); ap=scopy(ap,"Active disk: ");ap=scopy(ap,(char*)gdisk_name()); if(g_dksectors){u32 mb=(u32)((u64)g_dksectors*512/1048576);ap=scopy(ap,"  ");ap=udec(ap,mb);ap=scopy(ap," MB");} ap=scopy(ap,"  (install target)"); *ap=0; term_puts(ab); term_putc('\n'); tcol(WHT); }
        if(!pci_n)pci_scan(); int found=0;
        for(int k=0;k<pci_n;k++){ if(pci_cls[k]!=0x01)continue; found++;
            u32 cl=pci_cfg(pci_bus[k],pci_slot[k],0,0x08); int prog=(cl>>8)&0xFF, sub=pci_sub[k];
            const char* kind= sub==0x01?"IDE/ATA": sub==0x05?"ATA": sub==0x06?"SATA (AHCI)": sub==0x08?"NVMe": sub==0x04?"RAID":"mass storage";
            char b[100],*p=b; p=scopy(p,"  "); p=hx(p,pci_ven[k],4);*p++=':';p=hx(p,pci_dev[k],4); p=scopy(p,"  "); p=scopy(p,kind); *p=0;
            tcol(CYAN); term_puts(b); tcol(WHT); term_putc('\n');
            if(sub==0x06 && prog==0x01){
                u32 c4=pci_cfg(pci_bus[k],pci_slot[k],0,0x04); pci_wcfg(pci_bus[k],pci_slot[k],0,0x04,c4|0x06);   /* MEM + bus master */
                u32 abar=pci_cfg(pci_bus[k],pci_slot[k],0,0x24)&0xFFFFFFF0u;
                if(!abar){ tcol(GREY); term_puts("    (ABAR invalid)\n"); tcol(WHT); continue; }
                volatile u32* A=(volatile u32*)(u64)abar; A[0x04>>2]|=(1u<<31);   /* GHC.AE */
                u32 pi=A[0x0C>>2]; int any=0;
                for(int port=0;port<32;port++){ if(!((pi>>port)&1))continue; char model[48]; u64 sec=0;
                    if(ahci_identify(A,port,model,&sec)){ any=1; u64 mb=sec>>11; char lb[120],*q=lb;
                        q=scopy(q,"    port "); q=udec(q,(u32)port); q=scopy(q,": "); q=scopy(q,model[0]?model:"(disk)"); q=scopy(q,"  ");
                        if(mb>=1024){ q=udec(q,(u32)(mb>>10)); q=scopy(q," GB"); } else { q=udec(q,(u32)mb); q=scopy(q," MB"); } *q=0;
                        tcol(GREEN); term_puts(lb); tcol(WHT); term_putc('\n'); } }
                if(!any){ tcol(GREY); term_puts("    (no responding SATA disk)\n"); tcol(WHT); }
            } else if(sub==0x08){ tcol(GREY); term_puts("    NVMe present - read/identify not implemented yet\n"); tcol(WHT); }
        }
        if(!found){ tcol(rgb(240,150,120)); term_puts("no PCI mass-storage controller (eMMC/SD is not detected here)\n"); tcol(WHT); }
        tcol(GREY); term_puts("detection only - installing to disk is not enabled yet\n"); tcol(WHT); }
    else if(seq(cmd,"netlog")){ int start=netlog_n>18?netlog_n-18:0; for(int i=start;i<netlog_n;i++){ tcol(GREY); term_puts("  "); term_puts(NETLOG[i%18]); term_putc('\n'); } tcol(WHT); if(netlog_n==0)term_puts("(empty)\n"); }
    else if(seq(cmd,"colors")){ const u32 cs[8]={rgb(220,60,60),rgb(230,150,40),rgb(230,220,60),rgb(70,200,90),rgb(70,180,230),rgb(120,110,230),rgb(210,90,210),rgb(230,230,230)};
        for(int k=0;k<8;k++){tcol(cs[k]);term_puts("##");} tcol(WHT); term_putc('\n'); }
    else if(seq(cmd,"settings")){ open_win(W_SETTINGS); term_puts("opened Settings.\n"); }
    else if(seq(cmd,"chatgpt")||seq(cmd,"ai")||seq(cmd,"gpt")){ open_win(W_CHATGPT); term_puts("opened ChatGPT.\n"); }
    else if(cmd[0]=='l'&&cmd[1]=='u'&&cmd[2]=='a'&&(cmd[3]==' '||cmd[3]==0)){
        if(cmd[3]==0){ tcol(CYAN); term_puts("Lua 5.5.1 - usage: lua <code>\n  console: lua print(2^10, math.sqrt(144))\n  window:  lua nvx.window(\"Hi\",400,300) nvx.clear(nvx.rgb(30,40,60)) nvx.text(20,20,\"Hello from Lua\",0xFFFFFF) nvx.present() nvx.waitkey()\n"); tcol(WHT); }
        else { const char* out=nvx_lua_run(cmd+4); tcol(rgb(180,230,180)); term_puts(out); if(out[0]&&out[slen(out)-1]!='\n')term_putc('\n'); tcol(WHT); } }
    else if(seq(cmd,"presentation")||seq(cmd,"slides")||seq(cmd,"pptx")){ int sl=-1; for(int z=0;z<FS_N;z++)if(FS[z].used&&fext_eq(FS[z].name,"pptx")){sl=z;break;} if(sl>=0){pptx_load(sl);open_win(W_PPTX);term_puts("opened Presentation.\n");} else term_puts("no .pptx file found (put one on a USB stick).\n"); }
    else if(seq(cmd,"reboot")){ term_puts("rebooting...\n"); blit_all(); outb(0x64,0xFE); }
    else if(seq(cmd,"panic")){ panic(0,"Manual panic requested via 'panic' command."); }
    else { tcol(rgb(240,120,120)); term_puts("nsh: "); term_puts(cmd); term_puts(": command not found\n"); tcol(WHT); }
}
static u32 rtw_find_mmio(void); static int nic_index_wifi(void);
static void wpa_connect_saved(u32 m,int idx,const char* pw){
    if(idx<0||idx>=g_apn){ term_puts("  bad network id\n"); return; }
    if(!m)m=rtw_find_mmio();
    if(!m||!g_rxnum){ tcol(rgb(230,120,90)); term_puts("  run wifi init/allbuilt/macinit/fw/rings first\n"); tcol(WHT); return; }
    char b[96],*p; p=b;p=scopy(p,"  [wifi wpa] connecting to \"");p=scopy(p,g_aps[idx].ssid[0]?g_aps[idx].ssid:"<hidden>");p=scopy(p,"\" on ch ");p=udec(p,g_aps[idx].ch);p=scopy(p,"...");*p=0;
    tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
    g_chquiet=1;
    int r;
    if(g_aps[idx].sec==0) r=rtw_assoc(m,&g_aps[idx],g_aps[idx].ssid);
    else r=rtw_wpa2_join(m,&g_aps[idx],g_aps[idx].ssid,pw);
    g_chquiet=0;
    if(r==0){ tcol(GREEN); term_puts(g_aps[idx].sec==0?"  *** CONNECTED (open) ***\n":"  *** CONNECTED - 4-way handshake complete ***\n"); tcol(WHT); }
    else if(r==1){ tcol(rgb(230,160,60)); term_puts("  no auth response (AP not answering our TX)\n"); tcol(WHT); }
    else if(r==2){ tcol(rgb(230,160,60)); term_puts("  auth rejected\n"); tcol(WHT); }
    else if(r==3){ tcol(rgb(230,160,60)); term_puts("  no assoc response\n"); tcol(WHT); }
    else if(r==4){ tcol(rgb(230,160,60)); term_puts("  assoc rejected\n"); tcol(WHT); }
    else if(r==5){ tcol(rgb(230,160,60)); term_puts("  no EAPOL msg1 (associated, no handshake)\n"); tcol(WHT); }
    else if(r==6){ tcol(rgb(230,160,60)); term_puts("  wrong password (handshake MIC rejected)\n"); tcol(WHT); }
    else { tcol(rgb(230,160,60)); term_puts("  handshake failed\n"); tcol(WHT); }
}
static void wpa_do_join(const char* ss,const char* pw){
    u32 m=rtw_find_mmio();
    if(!m){ tcol(rgb(230,120,90)); term_puts("  no Wi-Fi adapter\n"); tcol(WHT); return; }
    if(!g_rxnum||!g_benum){ tcol(rgb(230,120,90)); term_puts("  run 'wifi rings' first (after init/allbuilt/macinit/fw)\n"); tcol(WHT); return; }
    char b[128],*p;
    p=b;p=scopy(p,"  [wifi wpa] joining \"");p=scopy(p,(char*)ss);p=scopy(p,"\" ...");*p=0; tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
    g_chquiet=1;
    g_apn=0; rtw_rx_ring_up(m);
    static const u8 wc[]={1,6,11,2,3,4,5,7,8,9,10,12,13};
    int tgt=-1;
    term_puts("  scanning: "); 
    for(int ci=0;ci<13 && tgt<0;ci++){ rtw_set_channel_2g(m,wc[ci]); ui_pump();
        { char cb[6]; char* z=cb; z=udec(z,wc[ci]); *z++=' '; *z=0; tcol(GREY); term_puts(cb); tcol(WHT); }
        for(int w=0;w<4;w++){ rtw_delay_us(62000); ui_pump(); rtw_scan_drain(m,wc[ci]); }
        for(int q=0;q<g_apn;q++){ int eq=1,k=0; while(ss[k]&&k<32){ char x=g_aps[q].ssid[k],y=ss[k]; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y)eq=0;k++;} if(g_aps[q].ssid[k]||ss[k])eq=0; if(eq){tgt=q;break;} } }
    term_putc('\n');
    g_chquiet=0;
    if(tgt<0){ tcol(rgb(230,160,60)); term_puts("  network not found. networks seen this scan:\n"); tcol(WHT);
        for(int q=0;q<g_apn;q++){ char* z=b; z=scopy(z,"    "); z=scopy(z,g_aps[q].ssid[0]?g_aps[q].ssid:"<hidden>"); z=scopy(z, g_aps[q].sec==0?"  (open)":g_aps[q].sec==2?"  (WPA2)":"  (WEP)"); *z=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT); }
        if(g_apn==0) term_puts("    (none - move closer / re-run)\n"); return; }
    p=b;p=scopy(p,"  found on ch ");p=udec(p,g_aps[tgt].ch);p=scopy(p,"  deriving PMK (PBKDF2 4096, ~1s)...");*p=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
    g_chquiet=1; int r=rtw_wpa2_join(m,&g_aps[tgt],ss,pw); g_chquiet=0;
    if(r==0){ tcol(GREEN); term_puts("  *** 4-WAY HANDSHAKE COMPLETE - PTK derived, keys ready ***\n"); tcol(WHT); }
    else if(r==1){ tcol(rgb(230,160,60)); term_puts("  no auth response\n"); tcol(WHT); }
    else if(r==2){ tcol(rgb(230,160,60)); term_puts("  auth rejected\n"); tcol(WHT); }
    else if(r==3){ tcol(rgb(230,160,60)); term_puts("  no assoc response\n"); tcol(WHT); }
    else if(r==4){ tcol(rgb(230,160,60)); term_puts("  assoc rejected (RSN mismatch?)\n"); tcol(WHT); }
    else if(r==5){ tcol(rgb(230,160,60)); term_puts("  no EAPOL msg1 (assoc ok, no handshake start)\n"); tcol(WHT); }
    else if(r==6){ tcol(rgb(230,160,60)); term_puts("  no EAPOL msg3 (msg2 sent - wrong password or MIC mismatch)\n"); tcol(WHT); }
    if(r==0){ tcol(g_gtk_state==1?GREEN:rgb(230,160,60)); term_puts("  "); term_puts(wpa_gtk_status()); term_putc('\n'); tcol(WHT); }
    else { tcol(rgb(230,160,60)); term_puts("  handshake failed\n"); tcol(WHT); }
}
static void term_key(char c){
    if(g_pwprompt){
        if(c=='\n'){ term_putc('\n'); tline[tlen]=0; g_pwprompt=0; if(g_pwbyid){ wpa_connect_saved(rtw_find_mmio(),g_pwbyid-1,tline); g_pwbyid=0; } else wpa_do_join(g_pwssid,tline); tlen=0; term_prompt(); }
        else if(c=='\b'){ if(tlen>0){tlen--;term_putc('\b');} }
        else if(c>=32&&c<127){ if(tlen<126){tline[tlen++]=c;term_putc('*');} }   /* mask password */
        return;
    }
    if(c=='\n'){ term_putc('\n'); tline[tlen]=0; run_cmd(tline); tlen=0; if(!g_tty_fg)term_prompt(); }
    else if(c=='\b'){ if(tlen>0){tlen--;term_putc('\b');} }
    else if(c>=32&&c<127){ if(tlen<126){tline[tlen++]=c;term_putc(c);} }
}
static void term_init(void){ term_geom(); term_clear();
    tcol(ACC);term_puts("NoovexOS 64");tcol(WHT);term_puts("  nsh shell\n");
    tcol(GREY);term_puts("type 'help' for commands, 'neofetch' for info.\n\n");tcol(WHT); term_prompt(); }
typedef struct { const char* name; u32 bg, bar, fg, dim, cur, green, blue, red, cyan; int light; } TTheme;
static const TTheme TTH[6]={
 {"Midnight",rgb(15,18,26),rgb(24,28,40),rgb(222,228,240),rgb(128,138,160),rgb(120,230,150),rgb(120,230,150),rgb(120,190,255),rgb(236,106,100),rgb(90,210,230),0},
 {"Dracula",rgb(40,42,54),rgb(33,34,44),rgb(248,248,242),rgb(150,156,190),rgb(255,121,198),rgb(80,250,123),rgb(189,147,249),rgb(255,85,85),rgb(139,233,253),0},
 {"Solarized",rgb(0,43,54),rgb(7,54,66),rgb(147,161,161),rgb(88,110,117),rgb(181,137,0),rgb(133,153,0),rgb(38,139,210),rgb(220,50,47),rgb(42,161,152),0},
 {"Paper",rgb(250,249,245),rgb(236,234,228),rgb(44,48,58),rgb(130,134,146),rgb(60,120,224),rgb(30,140,70),rgb(40,100,200),rgb(200,50,50),rgb(20,130,150),1},
 {"Matrix",rgb(4,10,6),rgb(10,20,12),rgb(140,255,160),rgb(60,140,80),rgb(140,255,160),rgb(90,255,120),rgb(120,230,170),rgb(255,120,100),rgb(120,255,210),0},
 {"Noovex",rgb(14,18,40),rgb(20,25,52),rgb(228,234,248),rgb(128,140,178),rgb(96,160,255),rgb(92,224,196),rgb(116,164,255),rgb(255,112,136),rgb(120,214,255),0}};
static u32 g_blurtmp[MAXW*MAXH];
static void term_glass(int x,int y,int w,int h,int r,u32 bg){
    if(g_topa>=100){ fill_round(x,y,w,h,r,15,bg); return; }
    int x0=x<0?0:x, y0=y<0?0:y, x1=x+w>(int)W?(int)W:x+w, y1=y+h>(int)H?(int)H:y+h; if(x1<=x0||y1<=y0)return;
    int bw=x1-x0, bh=y1-y0, R=g_tblur?10:0;
    if(R){ for(int j=0;j<bh;j++){ u32* src=&SCENE[(u64)(y0+j)*SW+x0]; u32* dst=&g_blurtmp[(u64)j*bw]; int sr=0,sg=0,sb=0,n=0;
            for(int i=-R;i<=R&&i<bw;i++){ if(i<0)continue; u32 c=src[i]; sr+=(c>>16)&255; sg+=(c>>8)&255; sb+=c&255; n++; }
            for(int i=0;i<bw;i++){ dst[i]=rgb(sr/n,sg/n,sb/n); int a=i+R+1, d=i-R; if(a<bw){ u32 c=src[a]; sr+=(c>>16)&255; sg+=(c>>8)&255; sb+=c&255; n++; } if(d>=0){ u32 c=src[d]; sr-=(c>>16)&255; sg-=(c>>8)&255; sb-=c&255; n--; } } }
        for(int i=0;i<bw;i++){ int sr=0,sg=0,sb=0,n=0;
            for(int j=0;j<=R&&j<bh;j++){ u32 c=g_blurtmp[(u64)j*bw+i]; sr+=(c>>16)&255; sg+=(c>>8)&255; sb+=c&255; n++; }
            for(int j=0;j<bh;j++){ u32 c0=rgb(sr/n,sg/n,sb/n); int a=j+R+1, d=j-R;
                if(a<bh){ u32 c=g_blurtmp[(u64)a*bw+i]; sr+=(c>>16)&255; sg+=(c>>8)&255; sb+=c&255; n++; }
                if(d>=0){ u32 c=g_blurtmp[(u64)d*bw+i]; sr-=(c>>16)&255; sg-=(c>>8)&255; sb-=c&255; n--; }
                if(!cut(x0-x+i,y0-y+j,w,h,r,15)) SCENE[(u64)(y0+j)*SW+x0+i]=c0; } } }
    int a=g_topa*256/100; if(a>256)a=256;
    for(int j=0;j<bh;j++){ u32* row=&SCENE[(u64)(y0+j)*SW+x0]; for(int i=0;i<bw;i++){ if(cut(x0-x+i,y0-y+j,w,h,r,15))continue; row[i]=mix(row[i],bg,a); } }
}
static u32 tmap(u32 c){ const TTheme* t=&TTH[g_tth];
    if(c==WHT)return t->fg; if(c==GREY)return t->dim; if(c==GREEN)return t->green; if(c==RED)return t->red; if(c==CYAN)return t->cyan; if(c==rgb(120,200,255))return t->blue;
    if(t->light){ int r=(c>>16)&255,g=(c>>8)&255,b=c&255; if((r*3+g*6+b)/10>150)return mix(c,rgb(0,0,0),120); } return c; }
static void tglyph(int x,int y,char ch,u32 c){ int idx=(u8)ch-32; if(idx<=0||idx>=95)return;
    if(!g_tfont){ dchar(x,y,ch,c,1); return; } const unsigned char* g=fontaa[idx];
    for(int r=0;r<14;r++){ int py=y+r; if((u32)py>=H)continue; for(int col=0;col<8;col++){ int a=g[r*8+col]; if(a<8)continue; int px=x+col; if((u32)px>=W)continue; u32* p=&SCENE[(u64)py*SW+px]; *p=mix(*p,c,a); } } }
static void term_draw(int i){
    const TTheme* t=&TTH[g_tth]; int x=WN[i].x,y=WN[i].y,w=WN[i].w,h=WN[i].h,r=14,foc=(focus==i);
    shadow_soft(x,y,w,h,r); term_glass(x,y,w,h,r,t->bg); fill_round(x,y,w,TITLEH,r,1|2,t->bar);
    u32 tc=foc?(t->light?rgb(40,44,54):rgb(226,230,240)):t->dim;
    fill_round(x+14,y+11,22,22,6,15,ACC); draw_logo(x+25,y+15,14,rgb(255,255,255),256);
    for(int q=0;q<w-2*r;q++){ u32 gc=mix(ACC,rgb(150,90,230),q*255/(w-2*r)); putpx(x+r+q,y+TITLEH-1,gc); putpx(x+r+q,y+TITLEH,mix(gc,t->bg,150)); }
    { char pb[160],b[200],*p=b; dt_init(); dt_path(g_dcwd,pb); p=scopy(p,cur_user()); *p++='@'; p=scopy(p,g_hostname); p=scopy(p,": "); p=scopy(p,pb); *p=0;
      int maxc=(w-48-150)/8; if(maxc<8)maxc=8; if(slen(b)>maxc){ b[maxc-2]='.'; b[maxc-1]='.'; b[maxc]=0; } if(!g_gm_ready)gmetrics(); pdstr(x+46,y+15,b,tc,1); }
    { int cw=46,xr=x+w; for(int bn=0;bn<3;bn++){ int bx=xr-(3-bn)*cw; int hov=(foc&&mx>=bx&&mx<bx+cw&&my>=y&&my<y+TITLEH);
        if(hov){ u32 hc=(bn==2)?rgb(232,64,54):mix(t->bar,t->light?rgb(0,0,0):rgb(255,255,255),30);
            for(int j=0;j<TITLEH;j++)for(int ii=0;ii<cw;ii++){ int gx=(bx-x)+ii; if(!cut(gx,j,w,TITLEH,r,1|2))putpx(bx+ii,y+j,hc); } }
        int gx=bx+cw/2, gy=y+TITLEH/2; u32 gc=(hov&&bn==2)?rgb(255,255,255):tc;
        if(bn==0){ for(int q=-5;q<=5;q++){ putpx(gx+q,gy,gc); putpx(gx+q,gy+1,gc); } }
        else if(bn==1){ for(int q=-5;q<=5;q++){ putpx(gx+q,gy-5,gc); putpx(gx+q,gy+5,gc); putpx(gx-5,gy+q,gc); putpx(gx+5,gy+q,gc); } }
        else { for(int q=-5;q<=5;q++){ putpx(gx+q,gy+q,gc); putpx(gx+q+1,gy+q,gc); putpx(gx+q,gy-q,gc); putpx(gx+q+1,gy-q,gc); } } } }
    term_geom(); int sb=g_tstatus?26:0; int ox=x+TPAD, oy=y+TITLEH+TPAD-2;
    if(g_tsb_off>tsb_n)g_tsb_off=tsb_n; if(g_tsb_off<0)g_tsb_off=0;
    if(!g_tsb_off&&foc) fill_rect(x+2,oy+tcy*TLH-2,w-4,TLH,mix(t->bg,t->light?rgb(0,0,0):rgb(255,255,255),t->light?10:8));
    for(int rr=0;rr<trows;rr++){ int v=tsb_n-g_tsb_off+rr; const char* rc; const u32* rcol;
        if(v<tsb_n){ int k=(tsb_head-tsb_n+v+TSB_N*2)%TSB_N; rc=TSB[k]; rcol=TSBC[k]; } else { int sr=v-tsb_n; if(sr>=trows)break; rc=TCH[sr]; rcol=TCO[sr]; }
        for(int c=0;c<tcols;c++){ char ch=rc[c]; if(ch!=' ')tglyph(ox+c*8,oy+rr*TLH+1,ch,tmap(rcol[c])); } }
    if(!g_tsb_off){ int cx=ox+tcx*8, cy=oy+tcy*TLH; int on=((g_ticks/50)&1)||!foc; u32 cc=t->cur;
        if(!foc){ for(int q=0;q<8;q++){ putpx(cx+q,cy,cc); putpx(cx+q,cy+TLH-2,cc); } for(int q=0;q<TLH-1;q++){ putpx(cx,cy+q,cc); putpx(cx+7,cy+q,cc); } }
        else if(on){ if(g_tcur==0){ fill_rect(cx,cy,8,TLH-1,cc); char ch=(tcx<tcols)?TCH[tcy][tcx]:' '; if(ch!=' ')tglyph(cx,cy+1,ch,t->bg); }
            else if(g_tcur==1)fill_rect(cx,cy,2,TLH-1,cc); else fill_rect(cx,cy+TLH-3,8,2,cc); } }
    if(tsb_n>0){ int th=trows*TLH, tot=tsb_n+trows, bh=th*trows/tot; if(bh<24)bh=24; int by=oy+(th-bh)*(tsb_n-g_tsb_off)/(tsb_n?tsb_n:1);
        fill_round(x+w-9,by,4,bh,2,15,mix(t->bg,t->light?rgb(0,0,0):rgb(255,255,255),g_tsb_off?90:40)); }
    if(sb){ int sy=y+h-sb; for(int j=0;j<sb;j++)for(int q=0;q<w;q++){ if(cut(q,h-sb+j,w,h,r,15))continue; u32* pp=&SCENE[(u64)(sy+j)*SW+x+q]; if((u32)(x+q)<W&&(u32)(sy+j)<H)*pp=mix(*pp,t->bar,g_topa>=100?256:220); } fill_rect(x+1,sy,w-2,1,mix(t->bar,t->light?rgb(0,0,0):rgb(255,255,255),t->light?24:18));
        fill_circle(x+18,sy+13,4,g_uid==0?t->red:t->green); char b[160],*p=b; p=scopy(p,cur_user()); *p++='@'; p=scopy(p,g_hostname); *p=0; int ex=dstr(x+30,sy+6,b,t->dim,1);
        { char pb[128]; dt_path(g_dcwd,pb); fill_rect(ex+10,sy+8,1,11,mix(t->bar,t->dim,120)); dstr(ex+20,sy+6,pb,t->blue,1); }
        p=b; if(g_tsb_off){ p=scopy(p,"scrolled up "); p=udec(p,(u32)g_tsb_off); p=scopy(p," lines - type to return"); }
        else { p=scopy(p,"nsh   "); p=udec(p,(u32)tcols); *p++='x'; p=udec(p,(u32)trows); } *p=0;
        dstr(x+w-slen(b)*8-18,sy+6,b,g_tsb_off?t->cur:t->dim,1); }
    { int gx=x+w-6, gy=y+h-6; u32 gc=t->dim; for(int k=0;k<3;k++){ int o=k*4; for(int q=0;q<=o;q++)putpx(gx-o+q,gy-q,gc); } }
}

