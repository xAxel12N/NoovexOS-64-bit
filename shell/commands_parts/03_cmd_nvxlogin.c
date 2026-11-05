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
    else if(seq(cmd,"bcstat")||seq(cmd,"bufcache")){
        extern unsigned long g_bc_hits,g_bc_miss;
        char b[90],*p=b; tcol(rgb(120,200,255)); term_puts("disk buffer cache\n"); tcol(WHT);
        p=scopy(p,"  hits   "); p=udec(p,(u32)g_bc_hits); *p=0; term_puts(b); term_putc('\n');
        p=b; p=scopy(p,"  misses "); p=udec(p,(u32)g_bc_miss); *p=0; term_puts(b); term_putc('\n');
        unsigned long tot=g_bc_hits+g_bc_miss; if(tot){ p=b; p=scopy(p,"  hit rate "); p=udec(p,(u32)(g_bc_hits*100/tot)); p=scopy(p,"%"); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); } }
    else if(seq(cmd,"df")||seq(cmd,"statfs")){
        extern int ux_statfs(const char*,unsigned long*); unsigned long st[8]; ux_statfs("/",st);
        tcol(rgb(120,200,255)); term_puts("Filesystem   Blocks     Used     Avail   Inodes  IFree\n"); tcol(WHT);
        char b[120],*p=b; p=scopy(p,"nvxfs      ");
        p=udec(p,(u32)st[2]); *p++=' ';*p++=' '; p=udec(p,(u32)(st[2]-st[3])); *p++=' ';*p++=' ';
        p=udec(p,(u32)st[4]); *p++=' ';*p++=' '; p=udec(p,(u32)st[5]); *p++=' ';*p++=' '; p=udec(p,(u32)st[6]); *p=0;
        term_puts(b); term_putc('\n');
        term_puts("(block size "); { char q[8],*r=q; r=udec(r,(u32)st[1]); *r=0; term_puts(q); } term_puts(" bytes)\n"); }
    else if(seq(cmd,"mounts")){ extern int proc_gen(const char*,char*,int); static char mb[512]; int n=proc_gen("/proc/mounts",mb,512); if(n>=0)term_puts(mb); }
    else if(seq(cmd,"slabinfo")){ extern void slab_stats(char*,int); static char sb[512]; slab_stats(sb,512); term_puts(sb); }
    else if(seq(cmd,"nvxwhoami")){ if(g_nvx_signedin){ tcol(GREEN);term_puts("signed in as ");term_puts(g_nvx_user[0]?g_nvx_user:g_nvx_name); if(g_nvx_email[0]){term_puts(" <");term_puts(g_nvx_email);term_puts(">");} term_putc('\n');tcol(WHT); } else { term_puts("not signed in - run: nvxlogin\n"); } }
    else if(seq(cmd,"nvxlogout")||seq(cmd,"signout")){ g_nvx_signedin=0; g_nvx_token[0]=g_nvx_user[0]=g_nvx_email[0]=0; term_puts("signed out\n"); }
    else if(seq(cmd,"nvxtest")){
        tcol(CYAN);term_puts("Probing noovexos.site API (raw responses):\n");tcol(WHT);
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
    else if(seq(cmd,"ver")||seq(cmd,"about")){ term_puts("NoovexOS 64  v"); term_puts((char*)g_buildsig.version); term_puts(" (native x86-64, from scratch)\n"); }
    else if(seq(cmd,"cpu")||seq(cmd,"cpuid")){ tcol(CYAN);term_puts("CPU: ");tcol(WHT);term_puts(g_brand);term_putc('\n');
        char b[64],*p=b;p=scopy(p,"long mode active   EFER.LMA=");*p++=(char)('0'+g_lma);*p=0;term_puts(b);term_putc('\n'); }
    else if(seq(cmd,"browser")){ extern int g_web_netbg, g_web_csp, g_ck_samesite; extern void power_prefs_save(void);
        const char* a=arg; while(*a==' ')a++; int* t=0; const char* nm=0;
        if(a[0]=='n'&&a[1]=='e'&&a[2]=='t'&&a[3]=='b'&&a[4]=='g'){ t=&g_web_netbg; nm="netbg"; a+=5; }
        else if(a[0]=='c'&&a[1]=='s'&&a[2]=='p'){ t=&g_web_csp; nm="csp"; a+=3; }
        else if(a[0]=='s'&&a[1]=='a'&&a[2]=='m'&&a[3]=='e'&&a[4]=='s'&&a[5]=='i'&&a[6]=='t'&&a[7]=='e'){ t=&g_ck_samesite; nm="samesite"; a+=8; }
        while(*a==' ')a++;
        if(t&&a[0]=='o'&&a[1]=='n'&&!a[2]){ *t=1; power_prefs_save(); term_puts(nm); term_puts(" on\n"); }
        else if(t&&a[0]=='o'&&a[1]=='f'&&a[2]=='f'&&!a[3]){ *t=0; power_prefs_save(); term_puts(nm); term_puts(" off\n"); }
        else if(t||*a){ term_puts("usage: browser [netbg|csp|samesite] [on|off]\n"); }
        else { term_puts("netbg     "); term_puts(g_web_netbg?"on ":"off"); term_puts("  page requests run in the background (off: the old way, one at a time)\n");
            term_puts("csp       "); term_puts(g_web_csp?"on ":"off"); term_puts("  Content Security Policy and frame-ancestors are enforced (off: only logged)\n");
            term_puts("samesite  "); term_puts(g_ck_samesite?"on ":"off"); term_puts("  SameSite cookie rules (off: cookies sent as before v10.35)\n");
            term_puts("change with: browser NAME on|off  (kept after a restart)\n"); } }
    else if(seq(cmd,"services")){ extern void svc_list(char*,int); extern int svc_index(const char*); extern int svc_set(int,int); extern void clock_sync_now(void);
        const char* a=arg; while(*a==' ')a++;
        int st=(a[0]=='s'&&a[1]=='t'&&a[2]=='a'&&a[3]=='r'&&a[4]=='t'&&a[5]==' ')?1:(a[0]=='s'&&a[1]=='t'&&a[2]=='o'&&a[3]=='p'&&a[4]==' ')?2:(a[0]=='s'&&a[1]=='y'&&a[2]=='n'&&a[3]=='c')?3:0;
        if(st==3){ extern void clock_sync_server(const char*); const char* h=a+4; while(*h==' ')h++; if(*h)clock_sync_server(h); else clock_sync_now(); term_puts("clock sync started (see services for the result)\n"); }
        else if(st){ const char* nm=a+(st==1?6:5); while(*nm==' ')nm++; int i=svc_index(nm); if(i<0)term_puts("services: no such service (run services to list them)\n"); else if(!svc_set(i,st==1))term_puts("services: this service is always on\n"); else { term_puts(st==1?"started ":"stopped "); term_puts(nm); term_putc('\n'); } }
        else if(a[0]){ term_puts("usage: services | services start NAME | services stop NAME | services sync\n"); }
        else { static char b[2048]; svc_list(b,sizeof b); term_puts(b); } }
    else if(seq(cmd,"time")){ int h,m,ss,y,mo,d;read_clock(&h,&m,&ss,&y,&mo,&d);char b[16],*p=b;p=u2(p,h);*p++=':';p=u2(p,m);*p++=':';p=u2(p,ss);*p=0;term_puts(b);term_putc('\n'); }
    else if(seq(cmd,"date")){ int h,m,ss,y,mo,d;read_clock(&h,&m,&ss,&y,&mo,&d);char b[16],*p=b;p=udec(p,y);*p++='-';p=u2(p,mo);*p++='-';p=u2(p,d);*p=0;term_puts(b);term_putc('\n'); }
    else if(seq(cmd,"uptime")){ char b[24],*p=b;p=scopy(p,"up ");p=udec(p,(u32)(g_ticks/100));p=scopy(p," seconds");*p=0;term_puts(b);term_putc('\n'); }
    else if(seq(cmd,"pwd")){ dt_init(); char pb[192]; dt_path(g_dcwd,pb); term_puts(pb); term_putc('\n'); }
    else if(sh_is_user_cmd(cmd)){ sh_user_cmds(cmd,arg); }
    else if(seq(cmd,"whoami")){ term_puts(cur_user()); term_putc('\n'); }
    else if(seq(cmd,"sudo")){ const char* a=arg; while(*a==' ')a++;
        if(!*a){ term_puts("usage: sudo <command>\n"); }
        else if(a[0]=='-'&&a[1]=='k'&&(!a[2]||a[2]==' ')){ g_sudo_until=0; term_puts("sudo: password will be asked again next time\n"); }
        else if(a[0]=='-'&&a[1]=='l'&&(!a[2]||a[2]==' ')){ if(acct_is_admin(g_uid)){ term_puts("User "); term_puts(cur_user()); term_puts(" may run all commands as root on "); term_puts(g_hostname); term_puts(" (password remembered for "); char t[12],*q=t; q=udec(q,(u32)g_sudo_min); *q=0; term_puts(t); term_puts(" min)\n"); } else sh_err(cur_user()," may not run sudo"); }
        else if(sudo_auth()){ int save=g_uid; g_uid=0; char cl[480]; int k=0; while(a[k]&&k<479){cl[k]=a[k];k++;} cl[k]=0; run_cmd(cl); g_uid=save; } }
    else if(seq(cmd,"sh")||seq(cmd,"source")||seq(cmd,".")||sh_is_sh(cmd)){
        char fn[96]; const char* f=sh_is_sh(cmd)?cmd:arg; int k=0; while(f[k]&&f[k]!=' '&&k<95){fn[k]=f[k];k++;} fn[k]=0;
        if(!fn[0]){ tcol(rgb(220,120,110)); term_puts("usage: sh <file.sh>\n"); tcol(WHT); }
        else { static char sb[8192]; int n=ux_read_file(fn,sb,8191); if(n<0)n=fat_read_file(fn,(u8*)sb,8191); if(n<0){ tcol(rgb(220,120,110)); term_puts("sh: cannot open "); term_puts(fn); term_putc('\n'); tcol(WHT);} else { sb[n]=0; const char* t=sb; if(sb[0]=='#'&&sb[1]=='!'){ while(*t&&*t!='\n')t++; if(*t)t++; } sh_run_script(t); } } }
    else if(seq(cmd,"set")&&!arg[0]){ for(int i=0;i<shv_n;i++){ term_puts(shv_name[i]); term_putc('='); term_puts(shv_val[i]); term_putc('\n'); } }
    else if(seq(cmd,"test")||seq(cmd,"[")){ g_lastrc = sh_truth(arg)?0:1; }
    else if(seq(cmd,"true")){ g_lastrc=0; }
    else if(seq(cmd,"false")){ g_lastrc=1; }
    else if(seq(cmd,"env")){ for(int i=0;i<NENV;i++) if(g_env[i].used){ term_puts(g_env[i].name); term_putc('='); term_puts(g_env[i].val); term_putc('\n'); } }
    else if(seq(cmd,"export")){ const char* a=arg; while(*a==' ')a++; char nm[24]; int i=0; while(a[i]&&a[i]!='='&&i<23){nm[i]=a[i];i++;} nm[i]=0; if(a[i]=='=')env_set(nm,a+i+1); }
    else if(seq(cmd,"cd")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        if(!*a||seq((char*)a,"~")){ extern char g_home[]; int hd=dt_resolve_dir(g_uid==0?"/root":g_home); g_dcwd=hd>=0?hd:dt_resolve_dir("/home"); }
        else { int d=dt_resolve_dir(a); if(d>=0&&!ux_dir_ok(d,1)){ tcol(rgb(240,120,120)); term_puts("cd: permission denied\n"); tcol(WHT); } else if(d>=0){g_dcwd=d; char pb[128]; dt_path(g_dcwd,pb); env_set("PWD",pb);} else { tcol(rgb(240,120,120)); term_puts("cd: no such directory\n"); tcol(WHT); } }
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
            if(k<FS_N){ FS[k].mode=(FS[k].mode&S_IFMT)|(nm&07777); if(!(FS[k].mode&S_IFMT))FS[k].mode|=S_IFREG; if(disk_ok)fs_sync(k); char mb[12]; char b2[40],*p=b2; p=scopy(p,"  "); p=scopy(p,mode_str(FS[k].mode,mb)); p=scopy(p," "); p=scopy(p,FS[k].name); *p=0; tcol(GREEN); term_puts(b2); term_putc('\n'); tcol(WHT); }
            else { int dd=dt_child(g_dcwd,a); if(dd>=0){ DT[dd].mode=S_IFDIR|(nm&07777); } else { tcol(rgb(240,120,120)); term_puts("chmod: no such file\n"); tcol(WHT); } } }
    }
    else if(seq(cmd,"mkdir")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        if(!*a){ term_puts("usage: mkdir <name>\n"); }
        else if(!perm_dir(g_dcwd)){ tcol(RED); term_puts("mkdir: Permission denied\n"); tcol(WHT); } else { int r=dt_mkdir_at(g_dcwd,a); if(r<0){tcol(RED);term_puts("mkdir failed (full or exists)\n");tcol(WHT);} else dt_save(); }
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
            if(k>=FS_N){ for(k=0;k<FS_N;k++) if(!FS[k].used){ fs_newslot(k); int n=0;while(a[n]&&n<63){FS[k].name[n]=a[n];n++;}FS[k].name[n]=0; FS[k].used=1; FS[k].folder=0; FS[k].dir=g_dcwd; FS[k].mode=0100644u; if(disk_ok)fs_sync(k); break; } if(k>=FS_N){tcol(RED);term_puts("touch: no space\n");tcol(WHT);} } }
    }
    else if(seq(cmd,"rm")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        int isd=0; int k=ux_lookup(a,&isd,0);
        if(k<0||isd){ tcol(rgb(240,120,120)); term_puts("rm: no such file\n"); tcol(WHT); }
        else if(ux_unlink(a)){ tcol(rgb(240,120,120)); term_puts("rm: permission denied\n"); tcol(WHT); }
    }
    else if(seq(cmd,"cp")){
        dt_init(); char src[64],dst[64]; const char* a=arg; while(*a==' ')a++; int n=0; while(*a&&*a!=' '&&n<63)src[n++]=*a++; src[n]=0; while(*a==' ')a++; n=0; while(*a&&*a!=' '&&n<63)dst[n++]=*a++; dst[n]=0;
        int s1; for(s1=0;s1<FS_N;s1++) if(FS[s1].used&&FS[s1].dir==g_dcwd&&seq(FS[s1].name,src))break;
        if(s1>=FS_N||!dst[0]){ tcol(RED);term_puts("usage: cp <src> <dst>\n");tcol(WHT); }
        else { int d1; for(d1=0;d1<FS_N;d1++) if(FS[d1].used&&FS[d1].dir==g_dcwd&&seq(FS[d1].name,dst))break; int fresh=0;
            if(d1==s1){ tcol(RED);term_puts("cp: source and destination are the same file\n");tcol(WHT); }
            else { if(d1>=FS_N){ fresh=1; for(d1=0;d1<FS_N;d1++) if(!FS[d1].used)break; }
            if(d1>=FS_N){tcol(RED);term_puts("cp: no space\n");tcol(WHT);}
            else if(!fresh&&!perm_file(d1,2)){ tcol(RED);term_puts("cp: permission denied\n");tcol(WHT); }
            else { if(fresh)fs_newslot(d1); int m=0;while(dst[m]&&m<63){FS[d1].name[m]=dst[m];m++;}FS[d1].name[m]=0;
                if(fs_set(d1,fsd(s1),FS[s1].len)){ tcol(RED);term_puts("cp: not enough memory\n");tcol(WHT); if(fresh)FS[d1].used=0; }
                else { FS[d1].used=1; FS[d1].folder=FS[s1].folder; FS[d1].dir=g_dcwd; if(fresh)FS[d1].mode=FS[s1].mode?FS[s1].mode:0100644u; ux_touch(d1,1); if(disk_ok)fs_sync(d1); } } } }
    }
    else if(seq(cmd,"mv")){
        dt_init(); char src[64],dst[64]; const char* a=arg; while(*a==' ')a++; int n=0; while(*a&&*a!=' '&&n<63)src[n++]=*a++; src[n]=0; while(*a==' ')a++; n=0; while(*a&&*a!=' '&&n<63)dst[n++]=*a++; dst[n]=0;
        int s1; for(s1=0;s1<FS_N;s1++) if(FS[s1].used&&FS[s1].dir==g_dcwd&&seq(FS[s1].name,src))break;
        if(s1>=FS_N||!dst[0]){ tcol(RED);term_puts("usage: mv <src> <dst>\n");tcol(WHT); }
        else { /* if dst is a directory, move into it; else rename */ int dd=dt_child(g_dcwd,dst);
            (void)dd; if(k_rename(src,dst)){ tcol(RED);term_puts("mv: cannot move ");term_puts(src);term_putc('\n');tcol(WHT); } }
    }
    else if(seq(cmd,"cat")){ { extern int proc_gen(const char*,char*,int); static char pb[16384]; int pg=proc_gen(arg,pb,16384); if(pg>=0){ term_puts(pb); goto cat_done2; } }
        dt_init(); int k=-1; { int isd=0; k=ux_lookup(arg,&isd,1); if(isd)k=-2; } if(k==-1){ int isd=0; if(ux_lookup(arg,&isd,0)<0){ for(k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==g_dcwd&&seq(FS[k].name,arg))break; if(k>=FS_N){ int sl=0; for(int z=0;arg[z];z++) if(arg[z]=='/')sl=1; k=-1; if(!sl)for(int z=0;z<FS_N;z++) if(FS[z].used&&seq(FS[z].name,arg)){ k=z; break; } } } } if(k>=0&&(FS[k].mode&0170000u)==0120000u)k=-1; if(k<-1)k=-1;
        if(k>=0&&k<FS_N&&FS[k].used&&(!ux_dir_ok(FS[k].dir,1)||((FS[k].mode&0777u)&&!ux_perm_ok(k,4)))){ tcol(rgb(240,120,120)); term_puts("cat: permission denied\n"); tcol(WHT); }
        else if(k>=0&&k<FS_N&&FS[k].used){term_puts(fsd(k));term_putc('\n');} else {tcol(rgb(240,120,120));term_puts("no such file\n");tcol(WHT);} }
    else if(seq(cmd,"edit")){ int k=-1; { int isd=0; k=ux_lookup(arg,&isd,1); if(isd)k=-2; } if(k==-1){ int isd=0; if(ux_lookup(arg,&isd,0)<0){ for(k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==g_dcwd&&seq(FS[k].name,arg))break; if(k>=FS_N){ int sl=0; for(int z=0;arg[z];z++) if(arg[z]=='/')sl=1; k=-1; if(!sl)for(int z=0;z<FS_N;z++) if(FS[z].used&&seq(FS[z].name,arg)){ k=z; break; } } } } if(k>=0&&(FS[k].mode&0170000u)==0120000u)k=-1; if(k<-1)k=-1;
        if(k>=0&&(!ux_dir_ok(FS[k].dir,1)||((FS[k].mode&0777u)&&!ux_perm_ok(k,4)))){ tcol(rgb(240,120,120)); term_puts("edit: permission denied\n"); tcol(WHT); } else if(k>=0&&FS[k].used){editor_open(k);term_puts("opened in editor.\n");} else {tcol(rgb(240,120,120));term_puts("no such file\n");tcol(WHT);} }
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
    cat_done2: ;
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
        if(pf_ready){ p=b;p=scopy(p,"  user page pool  ");p=udec(p,pf_n);p=scopy(p," frames (4 KB), ");p=udec(p,pframe_free_count());p=scopy(p," free");*p=0; tcol(WHT);term_puts(b);term_putc('\n'); }
        tcol(rgb(255,210,90)); term_puts("running virtual-memory isolation self-test...\n"); tcol(WHT);
        u32 r1=0,r2=0; int rc=vm_selftest(&r1,&r2);
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool on this machine\n"); }
        else if(rc==2){ tcol(RED); term_puts("  FAILED: could not allocate frames/tables\n"); }
        else { p=b;p=scopy(p,"  space1 @V=0x8000000000 = 0x");p=hx(p,r1,8);p=scopy(p,"   space2 = 0x");p=hx(p,r2,8);*p=0; tcol(CYAN);term_puts(b);term_putc('\n');
            if(rc==0){ tcol(GREEN); term_puts("  PASS: same virtual address holds different data per address space.\n");
                term_puts("  -> per-process paging + isolation works. (userspace stage 1+2 done)\n"); }
            else { tcol(RED); term_puts("  FAIL: isolation not achieved\n"); } }
        tcol(WHT); }
